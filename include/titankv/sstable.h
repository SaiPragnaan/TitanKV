#pragma once

#include "titankv/record.h"
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace titankv {

class SSTable {
 public:
  explicit SSTable(std::filesystem::path path);
  static void write(const std::filesystem::path& path, const std::vector<Record>& records);
  [[nodiscard]] std::optional<Record> get(const std::string& key) const;
  [[nodiscard]] std::vector<Record> all() const;
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }
  [[nodiscard]] std::uint64_t max_sequence() const { return max_sequence_; }

 private:
  struct IndexEntry { std::string key; std::uint64_t offset; };
  [[nodiscard]] bool may_contain(const std::string& key) const;
  std::filesystem::path path_;
  std::vector<IndexEntry> index_;
  std::vector<std::uint64_t> bloom_{128};  // 8 KiB, two-hash Bloom filter
  std::uint64_t max_sequence_{};
};

}  // namespace titankv
