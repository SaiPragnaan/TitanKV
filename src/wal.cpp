#include "titankv/wal.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace titankv {
namespace {
constexpr std::uint32_t kMagic = 0x544b564c;  // TKVL
constexpr std::size_t kHeaderSize = 4 + 8 + 1 + 4 + 4 + 4;

std::uint32_t checksum(const std::string& bytes) {
  std::uint32_t value = 2166136261u;
  for (unsigned char c : bytes) value = (value ^ c) * 16777619u;
  return value;
}
void put32(std::string& out, std::uint32_t value) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>(value >> (i * 8))); }
void put64(std::string& out, std::uint64_t value) { for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>(value >> (i * 8))); }
std::uint32_t get32(const char* p) { std::uint32_t n = 0; for (int i = 0; i < 4; ++i) n |= static_cast<std::uint32_t>(static_cast<unsigned char>(p[i])) << (i * 8); return n; }
std::uint64_t get64(const char* p) { std::uint64_t n = 0; for (int i = 0; i < 8; ++i) n |= static_cast<std::uint64_t>(static_cast<unsigned char>(p[i])) << (i * 8); return n; }
void write_all(int fd, const std::string& bytes) {
  std::size_t done = 0;
  while (done < bytes.size()) { const auto n = ::write(fd, bytes.data() + done, bytes.size() - done); if (n < 0 && errno == EINTR) continue; if (n <= 0) throw std::runtime_error("WAL write failed: " + std::string(std::strerror(errno))); done += static_cast<std::size_t>(n); }
}
}  // namespace

Wal::Wal(std::filesystem::path path, bool sync_writes) : path_(std::move(path)), sync_writes_(sync_writes) {
  if (!path_.parent_path().empty()) std::filesystem::create_directories(path_.parent_path());
  fd_ = ::open(path_.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);
  if (fd_ < 0) throw std::runtime_error("cannot open WAL: " + std::string(std::strerror(errno)));
}
Wal::~Wal() { if (fd_ >= 0) ::close(fd_); }

void Wal::append(const Record& r) {
  std::string bytes;
  bytes.reserve(kHeaderSize + r.key.size() + r.value.size());
  put32(bytes, kMagic); put64(bytes, r.sequence); bytes.push_back(r.tombstone ? 1 : 0);
  put32(bytes, static_cast<std::uint32_t>(r.key.size())); put32(bytes, static_cast<std::uint32_t>(r.value.size()));
  const std::string payload = r.key + r.value;
  put32(bytes, checksum(payload)); bytes += payload;
  std::lock_guard lock(mutex_);
  write_all(fd_, bytes);
  if (sync_writes_ && ::fdatasync(fd_) != 0) throw std::runtime_error("WAL sync failed: " + std::string(std::strerror(errno)));
}
void Wal::sync() { std::lock_guard lock(mutex_); if (::fdatasync(fd_) != 0) throw std::runtime_error("WAL sync failed: " + std::string(std::strerror(errno))); }

std::vector<Record> Wal::replay() const {
  const int fd = ::open(path_.c_str(), O_RDONLY);
  if (fd < 0) throw std::runtime_error("cannot read WAL");
  std::vector<Record> result;
  std::array<char, kHeaderSize> header{};
  auto read_full = [fd](char* data, std::size_t size) { std::size_t done = 0; while (done < size) { const auto n = ::read(fd, data + done, size - done); if (n < 0 && errno == EINTR) continue; if (n <= 0) return false; done += static_cast<std::size_t>(n); } return true; };
  while (read_full(header.data(), header.size())) {
    if (get32(header.data()) != kMagic) { ::close(fd); throw std::runtime_error("corrupt WAL magic"); }
    const auto key_size = get32(header.data() + 13), value_size = get32(header.data() + 17);
    if (key_size > (1U << 26) || value_size > (1U << 28)) { ::close(fd); throw std::runtime_error("invalid WAL record size"); }
    std::string payload(static_cast<std::size_t>(key_size) + value_size, '\0');
    if (!read_full(payload.data(), payload.size())) break;  // torn final write: safely ignore
    if (checksum(payload) != get32(header.data() + 21)) { ::close(fd); throw std::runtime_error("corrupt WAL checksum"); }
    result.push_back({get64(header.data() + 4), header[12] != 0, payload.substr(0, key_size), payload.substr(key_size)});
  }
  ::close(fd);
  return result;
}
}  // namespace titankv
