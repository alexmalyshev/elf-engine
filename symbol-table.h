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

  const Elf64_Sym& operator[](size_t idx) const {
    return syms_[idx];
  }

  constexpr size_t size() const {
    return syms_.size();
  }

  constexpr std::span<const Elf64_Sym> syms() const {
    return std::span{syms_};
  }

  constexpr std::span<const std::byte> bytes() const {
    return std::as_bytes(syms());
  }

 private:
  std::vector<Elf64_Sym> syms_;
};
