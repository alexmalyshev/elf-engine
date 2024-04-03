#pragma once

#include <cassert>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// String table encoded for ELF.
class ElfStringTable {
 public:
  ElfStringTable() {
    // All string tables begin with a NUL character.
    bytes_.push_back('\0');
  }

  // Insert a string into the symbol table, return its offset.
  uint32_t insert(std::string_view s) {
    auto startOffset = static_cast<uint32_t>(bytes_.size());
    assert(size_t{startOffset} == bytes_.size());

    // Strings are always encoded with a NUL terminator.
    bytes_.resize(bytes_.size() + s.size() + 1);
    std::memcpy(&bytes_[startOffset], s.data(), s.size());

    return startOffset;
  }

  constexpr std::span<const std::byte> bytes() const {
    return std::as_bytes(std::span{bytes_});
  }

 private:
  std::vector<uint8_t> bytes_;
};
