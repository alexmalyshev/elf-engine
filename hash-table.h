#pragma once

#include "string-table.h"
#include "symbol-table.h"

#include <cstdint>
#include <span>
#include <vector>

inline uint32_t hash(const char* name) {
    uint32_t h = 0;
    for (; *name; name++) {
        h = (h << 4) + *name;
        uint32_t g = h & 0xf0000000;
        if (g) {
            h ^= g >> 24;
        }
        h &= ~g;
    }
    return h;
}

class ElfHashTable {
 public:
  void build(const ElfSymbolTable& syms, const ElfStringTable& strings) {
    buckets_.reserve(syms.size() / 2);
    buckets_.resize(syms.size() / 2);

    chains_.reserve(syms.size());
    chains_.resize(syms.size());

    // Skip element zero as that's the undefined symbol.
    chainIdx_ = 1;
    for (size_t i = 1; i < syms.size(); ++i) {
      auto const bucketIdx = hash(strings.get(syms[i].st_name).data()) % buckets_.size();
      auto existingChainIdx = buckets_[bucketIdx];
      if (existingChainIdx == 0) {
        buckets_[bucketIdx] = chainIdx_;
      } else {
        chains_[chaseChainIdx(existingChainIdx)] = chainIdx_;
      }
      chainIdx_ = findNextChainIdx(chainIdx_);
    }
  }

  constexpr std::span<const uint32_t> buckets() const {
    return std::span{buckets_};
  }

  constexpr std::span<const uint32_t> chains() const {
    return std::span{chains_};
  }

  constexpr size_t size_bytes() const {
    return (sizeof(uint32_t) * 2) + buckets().size_bytes() + chains().size_bytes();
  }

 private:
  uint32_t findNextChainIdx(uint32_t idx) const {
    if (idx >= chains_.size()) {
      throw std::runtime_error{"Chains array index is too big"};
    }
    bool reset = false;
    while (chains_[idx] != 0) {
      ++idx;
      if (idx >= chains_.size()) {
        idx = 1;
        if (reset) {
          throw std::runtime_error{"Already reset chain index once before, infinite loop"};
        }
        reset = true;
      }
    }
    return idx;
  }

  uint32_t chaseChainIdx(uint32_t idx) const {
    // TODO: Would be nice to verify this doesn't infinite loop.
    while (chains_[idx] != 0) {
      idx = chains_[idx];
    }
    return idx;
  }

  std::vector<uint32_t> buckets_;
  std::vector<uint32_t> chains_;
  uint32_t chainIdx_{0};
};
