#include "titankv/engine.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace titankv {
Engine::Engine(Options options)
    : options_(std::move(options)), wal_(options_.data_dir / "wal.log", options_.sync_writes) {
  if (options_.data_dir.empty()) throw std::invalid_argument("data_dir is required");
  std::filesystem::create_directories(options_.data_dir);
  load_tables();
  for (const auto& record : wal_.replay()) apply_recovered(record);
  compactor_ = std::thread(&Engine::compaction_loop, this);
}
Engine::~Engine() { stopping_ = true; if (compactor_.joinable()) compactor_.join(); try { flush(); } catch (...) {} }
void Engine::load_tables() {
  std::vector<std::filesystem::path> paths;
  for (const auto& entry : std::filesystem::directory_iterator(options_.data_dir)) if (entry.is_regular_file() && entry.path().filename().string().starts_with("sst-")) paths.push_back(entry.path());
  std::sort(paths.begin(), paths.end());
  for (const auto& path : paths) { auto table = std::make_shared<SSTable>(path); sequence_ = std::max(sequence_, table->max_sequence()); tables_.push_back(std::move(table)); }
  next_table_id_ = static_cast<std::uint64_t>(tables_.size()) + 1;
}
void Engine::apply_recovered(const Record& record) { sequence_ = std::max(sequence_, record.sequence); auto it = memtable_.find(record.key); if (it == memtable_.end() || it->second.sequence < record.sequence) memtable_[record.key] = record; }
void Engine::set(std::string key, std::string value) {
  if (key.empty()) throw std::invalid_argument("key must not be empty");
  std::unique_lock lock(mutex_);
  if (options_.failure_injector) options_.failure_injector->trigger(FailurePoint::before_wal_write, "injected crash before WAL");
  Record record{++sequence_, false, std::move(key), std::move(value)}; wal_.append(record);
  if (options_.failure_injector) options_.failure_injector->trigger(FailurePoint::after_wal_write, "injected crash after WAL");
  memtable_[record.key] = std::move(record);
  if (options_.failure_injector) options_.failure_injector->trigger(FailurePoint::after_memory_update, "injected crash after memory update");
  if (!options_.sync_writes && ++unsynced_writes_ >= options_.wal_sync_interval) { wal_.sync(); unsynced_writes_ = 0; } maybe_flush_locked();
}
void Engine::erase(std::string key) { if (key.empty()) throw std::invalid_argument("key must not be empty"); std::unique_lock lock(mutex_); Record record{++sequence_, true, std::move(key), {}}; wal_.append(record); memtable_[record.key] = std::move(record); if (!options_.sync_writes && ++unsynced_writes_ >= options_.wal_sync_interval) { wal_.sync(); unsynced_writes_ = 0; } maybe_flush_locked(); }
std::optional<std::string> Engine::get(const std::string& key) const {
  std::shared_lock lock(mutex_); std::optional<Record> best;
  if (const auto it = memtable_.find(key); it != memtable_.end()) best = it->second;
  for (auto it = tables_.rbegin(); it != tables_.rend(); ++it) if (const auto candidate = (*it)->get(key); candidate && (!best || candidate->sequence > best->sequence)) best = candidate;
  if (!best || best->tombstone) return std::nullopt;
  return best->value;
}
std::filesystem::path Engine::next_table_path_locked() { std::ostringstream name; name << "sst-" << std::setw(20) << std::setfill('0') << next_table_id_++ << ".sst"; return options_.data_dir / name.str(); }
void Engine::maybe_flush_locked() { if (memtable_.size() >= options_.memtable_max_records) { std::vector<Record> records; records.reserve(memtable_.size()); for (const auto& [_, record] : memtable_) records.push_back(record); const auto path=next_table_path_locked(); const auto temp=path.string()+".tmp"; SSTable::write(temp,records); std::filesystem::rename(temp,path); tables_.push_back(std::make_shared<SSTable>(path)); memtable_.clear(); } }
void Engine::flush() { std::unique_lock lock(mutex_); if (!memtable_.empty()) { const auto saved = options_.memtable_max_records; options_.memtable_max_records = 0; maybe_flush_locked(); options_.memtable_max_records = saved; } }
void Engine::compact() {
  std::unique_lock lock(mutex_); if (tables_.size() < 2) return; std::map<std::string, Record> newest;
  for (const auto& table : tables_) for (auto record : table->all()) { auto it=newest.find(record.key); if (it==newest.end()||it->second.sequence<record.sequence) newest[record.key]=std::move(record); }
  std::vector<Record> merged; for(auto& [_,r]:newest) merged.push_back(std::move(r)); const auto path=next_table_path_locked(); const auto temp=path.string()+".tmp"; SSTable::write(temp,merged); std::filesystem::rename(temp,path); const auto old=std::move(tables_); tables_={std::make_shared<SSTable>(path)}; for(const auto& table:old) std::filesystem::remove(table->path());
}
void Engine::compaction_loop() { while (!stopping_) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); if (!stopping_ && table_count() >= options_.compaction_trigger) compact(); } }
std::uint64_t Engine::sequence() const { std::shared_lock lock(mutex_); return sequence_; }
std::size_t Engine::table_count() const { std::shared_lock lock(mutex_); return tables_.size(); }
}  // namespace titankv
