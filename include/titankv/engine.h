#pragma once

#include "titankv/record.h"
#include "titankv/fault.h"
#include "titankv/sstable.h"
#include "titankv/wal.h"
#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace titankv {

struct Options {
  std::filesystem::path data_dir;
  std::size_t memtable_max_records{1024};
  std::size_t compaction_trigger{4};
  bool sync_writes{true};
  std::size_t wal_sync_interval{1};
  FailureInjector* failure_injector{nullptr};
};

class Engine {
 public:
  explicit Engine(Options options);
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  void set(std::string key, std::string value);
  void erase(std::string key);
  [[nodiscard]] std::optional<std::string> get(const std::string& key) const;
  void flush();
  void compact();
  [[nodiscard]] std::uint64_t sequence() const;
  [[nodiscard]] std::size_t table_count() const;

 private:
  void load_tables();
  void apply_recovered(const Record& record);
  void maybe_flush_locked();
  [[nodiscard]] std::filesystem::path next_table_path_locked();
  void compaction_loop();

  Options options_;
  Wal wal_;
  mutable std::shared_mutex mutex_;
  std::map<std::string, Record> memtable_;
  std::vector<std::shared_ptr<SSTable>> tables_;  // oldest to newest
  std::uint64_t sequence_{};
  std::uint64_t next_table_id_{1};
  std::size_t unsynced_writes_{};
  std::atomic<bool> stopping_{false};
  std::thread compactor_;
};

}  // namespace titankv
