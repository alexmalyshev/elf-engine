#include "elf.h"
#include "runtime.h"

#include <array>
#include <cassert>
#include <fstream>
#include <print>
#include <vector>

namespace {

// Section 0 is the null section which is zeroed out.
constexpr size_t kTextSectionHeaderIdx = 1;
constexpr size_t kSymbolSectionHeaderIdx = 2;
constexpr size_t kSymbolStringSectionHeaderIdx = 3;
constexpr size_t kSectionStringSectionHeaderIdx = 4;
constexpr size_t kNumSections = 5;

// Note: These are pulled out of thin air.
constexpr size_t kTextStart = 0x1000000;
constexpr size_t kTextSize = 96;

extern "C" size_t collatz_conjecture(uint64_t n) {
  auto fn = functionTable[0];
  auto collatz_step = reinterpret_cast<uint64_t(*)(uint64_t)>(fn);

  size_t steps = 0;
  while (n != 1) {
    n = collatz_step(n);
    ++steps;
  }

  return steps;
}

struct ElfHeader {
  Elf64_Ehdr fileHeader;
  Elf64_Phdr progHeader;
  std::array<Elf64_Shdr, kNumSections> sectHeaders;
};

// String table encoded for ELF.
class ElfStringTable {
 public:
  ElfStringTable() {
    // All string tables begin with a NUL character.
    bytes_.push_back(0);
  }

  // Insert a string into the symbol table, return its offset.
  uint32_t insert(std::string_view s) {
    size_t startOffset = bytes_.size();
    // TODO: Assert startOffset fits in 32-bits.

    // Strings are always encoded with a NUL terminator.
    bytes_.resize(bytes_.size() + s.size() + 1);
    std::memcpy(&bytes_[startOffset], s.data(), s.size());

    return static_cast<uint32_t>(startOffset);
  }

  constexpr const uint8_t* start() const {
    return bytes_.data();
  }

  constexpr size_t size() const {
    return bytes_.size();
  }

 private:
  std::vector<uint8_t> bytes_;
};

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

  const uint8_t* start() const {
    return reinterpret_cast<const uint8_t*>(syms_.data());
  }

  constexpr size_t size() const {
    return syms_.size() * sizeof(syms_[0]);
  }

 private:
  std::vector<Elf64_Sym> syms_;
};

void initSymbols(ElfSymbolTable& symbols, ElfStringTable& symbolNames) {
  Elf64_Sym sym;
  memset(&sym, 0, sizeof(sym));
  sym.st_name = symbolNames.insert("collatz_conjecture");
  sym.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
  sym.st_shndx = kTextSectionHeaderIdx;
  sym.st_value = kTextStart;
  sym.st_size = kTextSize;

  symbols.insert(std::move(sym));
}

void initFileHeader(Elf64_Ehdr& header) {
  memset(&header, 0, sizeof(header));

  header.e_ident[EI_MAG0] = ELFMAG0;
  header.e_ident[EI_MAG1] = ELFMAG1;
  header.e_ident[EI_MAG2] = ELFMAG2;
  header.e_ident[EI_MAG3] = ELFMAG3;

  header.e_ident[EI_CLASS] = ELFCLASS64;

  header.e_ident[EI_DATA] = ELFDATA2LSB;
  header.e_ident[EI_VERSION] = EV_CURRENT;
  header.e_ident[EI_OSABI] = ELFOSABI_LINUX;

  header.e_type = ET_DYN;
  header.e_machine = EM_X86_64;
  header.e_version = EV_CURRENT;

  header.e_phoff = offsetof(ElfHeader, progHeader);
  header.e_shoff = offsetof(ElfHeader, sectHeaders);
  header.e_ehsize = sizeof(header);
  header.e_phentsize = sizeof(Elf64_Phdr);
  header.e_phnum = 1;
  header.e_shentsize = sizeof(Elf64_Shdr);
  header.e_shnum = kNumSections;
  header.e_shstrndx = kSectionStringSectionHeaderIdx;
}

void initProgramHeader(Elf64_Phdr& header) {
  memset(&header, 0, sizeof(header));

  header.p_type = PT_LOAD;
  header.p_flags = PF_R | PF_X;
  header.p_offset = sizeof(ElfHeader);
  header.p_vaddr = kTextStart;
  header.p_filesz = kTextSize;
  header.p_memsz = kTextSize;
  header.p_align = 0x1000;
}

void initSectionHeader(Elf64_Shdr& header) {
  memset(&header, 0, sizeof(header));
}

void initHeader(ElfHeader& header) {
  initFileHeader(header.fileHeader);
  initProgramHeader(header.progHeader);
  for (auto& sectHeader : header.sectHeaders) {
    initSectionHeader(sectHeader);
  }
}

void initSections(
  ElfHeader& header,
  ElfSymbolTable& symbols,
  ElfStringTable& symbolNames,
  ElfStringTable& sectionNames) {
  // Sections start right after the header table.
  uint32_t sectionOffset = sizeof(ElfHeader);

  // .text

  auto& text = header.sectHeaders[kTextSectionHeaderIdx];
  text.sh_name = sectionNames.insert(".text");
  text.sh_type = SHT_PROGBITS;
  text.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
  text.sh_addr = header.progHeader.p_vaddr;
  text.sh_offset = sectionOffset;
  text.sh_size = kTextSize;
  text.sh_addralign = 0x1000;
  assert((text.sh_addr % text.sh_addralign) == 0);
  sectionOffset += text.sh_size;

  // .symtab

  auto& symtab = header.sectHeaders[kSymbolSectionHeaderIdx];
  symtab.sh_name = sectionNames.insert(".symtab");
  symtab.sh_type = SHT_SYMTAB;
  symtab.sh_flags = SHF_INFO_LINK;
  symtab.sh_offset = sectionOffset;
  symtab.sh_size = symbols.size();
  symtab.sh_link = kSymbolStringSectionHeaderIdx;
  // Index of the first non-null symbol.
  symtab.sh_info = 1;
  symtab.sh_entsize = sizeof(Elf64_Sym);
  sectionOffset += symtab.sh_size;

  // .strtab

  auto& strtab = header.sectHeaders[kSymbolStringSectionHeaderIdx];
  strtab.sh_name = sectionNames.insert(".strtab");
  strtab.sh_type = SHT_STRTAB;
  strtab.sh_flags = SHF_STRINGS;
  strtab.sh_offset = sectionOffset;
  strtab.sh_size = symbolNames.size();
  sectionOffset += strtab.sh_size;

  // .shstrtab

  auto& shstrtab = header.sectHeaders[kSectionStringSectionHeaderIdx];
  shstrtab.sh_name = sectionNames.insert(".shstrtab");
  shstrtab.sh_type = SHT_STRTAB;
  shstrtab.sh_flags = SHF_STRINGS;
  shstrtab.sh_offset = sectionOffset;
  shstrtab.sh_size = sectionNames.size();
  sectionOffset += shstrtab.sh_size;
}

template <class T>
void write(std::ostream& os, T* data, size_t size) {
  os.write(reinterpret_cast<const char*>(data), size);
  assert(!os.bad());
}

} // namespace

int main(int, char**) {
  ElfSymbolTable symbols;
  ElfStringTable symbolNames;
  initSymbols(symbols, symbolNames);

  ElfHeader header;
  initHeader(header);

  ElfStringTable sectionNames;
  initSections(header, symbols, symbolNames, sectionNames);

  std::ofstream out{"/tmp/test.so"};
  write(out, &header, sizeof(header));
  write(out, collatz_conjecture, kTextSize);
  write(out, symbols.start(), symbols.size());
  write(out, symbolNames.start(), symbolNames.size());
  write(out, sectionNames.start(), sectionNames.size());

  return 0;
}