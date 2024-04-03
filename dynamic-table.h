#pragma once

#include <elf.h>

#include <span>
#include <vector>

class ElfDynamicTable {
 public:
  ElfDynamicTable() {
    // Dynamic table always terminates with a null item.
    Elf64_Dyn dyn;
    std::memset(&dyn, 0, sizeof(dyn));
    dyns_.emplace_back(std::move(dyn));
  }

  template <class T>
  void insert(T&& dyn) {
    // Append the new item then swap with the null item.
    dyns_.emplace_back(std::forward<T>(dyn));
    auto const len = dyns_.size();
    std::swap(dyns_[len - 2], dyns_[len - 1]);
  }

  constexpr std::span<const std::byte> bytes() const {
    return std::as_bytes(std::span{dyns_});
  }

 private:
  std::vector<Elf64_Dyn> dyns_;
};
