#include "elf.h"
#include "runtime.h"

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <print>
#include <vector>

namespace {

enum class SegmentIdx : uint32_t {
  Text,
  Dynsym,
  Total,
};

enum class SectionIdx : uint32_t {
  Null,
  Text,
  Dynsym,
  Dynstr,
  Symtab,
  Strtab,
  Shstrtab,
  Total,
};

constexpr uint32_t raw(SegmentIdx idx) {
  return static_cast<uint32_t>(idx);
}

constexpr uint32_t raw(SectionIdx idx) {
  return static_cast<uint32_t>(idx);
}

// Note: These are pulled out of thin air.
constexpr size_t kTextStart = 0x1000000;
constexpr size_t kTextSize = 96;
constexpr size_t kTextAlign = 0x1000;
constexpr size_t kDynsymStart = 0x2000000;
constexpr size_t kDynstrStart = 0x3000000;

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
  Elf64_Phdr& getSegmentHeader(SegmentIdx idx) {
    return progHeaders[raw(idx)];
  }

  Elf64_Shdr& getSectionHeader(SectionIdx idx) {
    return sectHeaders[raw(idx)];
  }

  Elf64_Ehdr fileHeader;
  std::array<Elf64_Phdr, raw(SegmentIdx::Total)> progHeaders;
  std::array<Elf64_Shdr, raw(SectionIdx::Total)> sectHeaders;
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
  sym.st_shndx = raw(SectionIdx::Text);
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

  header.e_phoff = offsetof(ElfHeader, progHeaders);
  header.e_shoff = offsetof(ElfHeader, sectHeaders);
  header.e_ehsize = sizeof(header);
  header.e_phentsize = sizeof(Elf64_Phdr);
  header.e_phnum = raw(SegmentIdx::Total);
  header.e_shentsize = sizeof(Elf64_Shdr);
  header.e_shnum = raw(SectionIdx::Total);
  header.e_shstrndx = raw(SectionIdx::Shstrtab);
}

void initProgramHeader(Elf64_Phdr& header) {
  memset(&header, 0, sizeof(header));
}

void initSectionHeader(Elf64_Shdr& header) {
  memset(&header, 0, sizeof(header));
}

void initHeader(ElfHeader& header) {
  initFileHeader(header.fileHeader);
  for (auto& progHeader : header.progHeaders) {
    initProgramHeader(progHeader);
  }
  for (auto& sectHeader : header.sectHeaders) {
    initSectionHeader(sectHeader);
  }
}

void initSegments(
  ElfHeader& header,
  const ElfSymbolTable& symbols,
  const ElfStringTable& symbolNames
) {
  // Segments start right after the header table.
  uint32_t segmentOffset = sizeof(ElfHeader);

  // Readable and executable segment for .text

  auto& text = header.getSegmentHeader(SegmentIdx::Text);
  text.p_type = PT_LOAD;
  text.p_flags = PF_R | PF_X;
  text.p_offset = segmentOffset;
  text.p_vaddr = kTextStart;
  text.p_filesz = kTextSize;
  text.p_memsz = kTextSize;
  text.p_align = kTextAlign;
  segmentOffset += text.p_filesz;

  // Readable segment for .dynsym and .dynstr

  auto& dynsym = header.getSegmentHeader(SegmentIdx::Dynsym);
  dynsym.p_type = PT_LOAD;
  dynsym.p_flags = PF_R;
  dynsym.p_offset = segmentOffset;
  dynsym.p_vaddr = kDynsymStart;
  dynsym.p_filesz = symbols.size() + symbolNames.size();
  dynsym.p_memsz = dynsym.p_filesz;
  segmentOffset += dynsym.p_filesz;
}

void initSections(
  ElfHeader& header,
  ElfSymbolTable& symbols,
  ElfStringTable& symbolNames,
  ElfStringTable& sectionNames) {
  // Sections start right after the header table.
  uint32_t sectionOffset = sizeof(ElfHeader);

  // .text

  auto& text = header.getSectionHeader(SectionIdx::Text);
  text.sh_name = sectionNames.insert(".text");
  text.sh_type = SHT_PROGBITS;
  text.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
  text.sh_addr = header.getSegmentHeader(SegmentIdx::Text).p_vaddr;
  text.sh_offset = sectionOffset;
  text.sh_size = kTextSize;
  text.sh_addralign = kTextAlign;
  assert(text.sh_addralign == 0 || (text.sh_addr % text.sh_addralign) == 0);
  sectionOffset += text.sh_size;

  // .dynsym

  auto& dynsym = header.getSectionHeader(SectionIdx::Dynsym);
  dynsym.sh_name = sectionNames.insert(".dynsym");
  dynsym.sh_type = SHT_DYNSYM;
  dynsym.sh_flags = SHF_ALLOC | SHF_INFO_LINK;
  dynsym.sh_addr = header.getSegmentHeader(SegmentIdx::Dynsym).p_vaddr;
  dynsym.sh_offset = sectionOffset;
  dynsym.sh_size = symbols.size();
  dynsym.sh_link = raw(SectionIdx::Dynstr);
  // Index of the first non-null symbol.
  dynsym.sh_info = 1;
  dynsym.sh_entsize = sizeof(Elf64_Sym);
  sectionOffset += dynsym.sh_size;

  // .dynstr
  auto& dynstr = header.getSectionHeader(SectionIdx::Dynstr);
  dynstr.sh_name = sectionNames.insert(".dynstr");
  dynstr.sh_type = SHT_STRTAB;
  dynstr.sh_flags = SHF_ALLOC; // TODO: Should this have SHF_STRINGS?
  dynstr.sh_addr = kDynstrStart;
  dynstr.sh_offset = sectionOffset;
  dynstr.sh_size = symbolNames.size();
  sectionOffset += dynstr.sh_size;

  // .symtab

  auto& symtab = header.getSectionHeader(SectionIdx::Symtab);
  symtab.sh_name = sectionNames.insert(".symtab");
  symtab.sh_type = SHT_SYMTAB;
  symtab.sh_flags = SHF_INFO_LINK;
  symtab.sh_offset = sectionOffset;
  symtab.sh_size = symbols.size();
  symtab.sh_link = raw(SectionIdx::Strtab);
  // Index of the first non-null symbol.
  symtab.sh_info = 1;
  symtab.sh_entsize = sizeof(Elf64_Sym);
  sectionOffset += symtab.sh_size;

  // .strtab

  auto& strtab = header.getSectionHeader(SectionIdx::Strtab);
  strtab.sh_name = sectionNames.insert(".strtab");
  strtab.sh_type = SHT_STRTAB;
  strtab.sh_flags = SHF_STRINGS;
  strtab.sh_offset = sectionOffset;
  strtab.sh_size = symbolNames.size();
  sectionOffset += strtab.sh_size;

  // .shstrtab

  auto& shstrtab = header.getSectionHeader(SectionIdx::Shstrtab);
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

int main(int argc, char** argv) {
  if (argc != 2) {
    std::println(
      stderr,
      "Error: Takes exactly one argument (the path to write the shared object"
    );
    return 1;
  }

  auto outPath = argv[1];

  ElfSymbolTable symbols;
  ElfStringTable symbolNames;
  initSymbols(symbols, symbolNames);

  ElfHeader header;
  initHeader(header);
  initSegments(header, symbols, symbolNames);

  ElfStringTable sectionNames;
  initSections(header, symbols, symbolNames, sectionNames);

  std::ofstream out{outPath};

  // All the headers (file, program, sections).
  write(out, &header, sizeof(header));

  // .text
  write(out, collatz_conjecture, kTextSize);
  // .dynsym
  write(out, symbols.start(), symbols.size());
  // .dynstr
  write(out, symbolNames.start(), symbolNames.size());
  // .symtab
  write(out, symbols.start(), symbols.size());
  // .strtab
  write(out, symbolNames.start(), symbolNames.size());
  // .shstrtab
  write(out, sectionNames.start(), sectionNames.size());

  std::filesystem::permissions(
    outPath,
    std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
    std::filesystem::perm_options::add
  );

  return 0;
}
