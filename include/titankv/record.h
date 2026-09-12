#pragma once

#include <cstdint>
#include <string>

namespace titankv {

struct Record {
  std::uint64_t sequence{};
  bool tombstone{};
  std::string key;
  std::string value;
};

}  // namespace titankv
