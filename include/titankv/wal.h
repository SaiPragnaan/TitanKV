#pragma once

#include "titankv/record.h"
#include <filesystem>
#include <mutex>
#include <vector>

namespace titankv {

class Wal {
 public:
  explicit Wal(std::filesystem::path path, bool sync_writes = true);
  ~Wal();
  Wal(const Wal&) = delete;
  Wal& operator=(const Wal&) = delete;

  void append(const Record& record);
  void sync();
  [[nodiscard]] std::vector<Record> replay() const;

 private:
  std::filesystem::path path_;
  int fd_{-1};
  bool sync_writes_;
  mutable std::mutex mutex_;
};

}  // namespace titankv
