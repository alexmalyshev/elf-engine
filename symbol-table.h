#pragma once

#include <elf.h>

#include <cstring>
#include <span>
#include <vector>

// Symbol table encoded for ELF.
class ElfSymbolTable {
 public:
  ElfSymbolTable() {
    // Symbol table must always start with an undefined symbol.
    Elf64_Sym null_sym;
    std::memset(&null_sym, 0, sizeof(null_sym));
    insert(null_sym);
  }

  template <class T>
  void insert(T&& sym) {
    syms_.emplace_back(std::forward<T>(sym));
  }

  constexpr std::span<const std::byte> bytes() const {
    return std::as_bytes(std::span{syms_});
  }

 private:
  std::vector<Elf64_Sym> syms_;
};
