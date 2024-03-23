#include "dynamic-table.h"
#include "runtime.h"
#include "string-table.h"
#include "symbol-table.h"

#include <elf.h>

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <print>
#include <span>
#include <vector>

namespace {

enum class SegmentIdx : uint32_t {
  Text,
  Dynsym,

#ifdef DYNAMIC_FIXED
  Dynamic,
#endif
  Total,
};

enum class SectionIdx : uint32_t {
  Null,
  Text,
  Dynsym,
  Dynstr,
  Dynamic,
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
constexpr size_t kDynamicStart = 0x3000000;

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

// All headers in an ELF file.
struct ElfHeaders {
  ElfHeaders() {
    initFileHeader();
    memset(&segmentHeaders, 0, sizeof(segmentHeaders));
    memset(&sectionHeaders, 0, sizeof(sectionHeaders));
  }

  Elf64_Phdr& getSegmentHeader(SegmentIdx idx) {
    return segmentHeaders[raw(idx)];
  }

  Elf64_Shdr& getSectionHeader(SectionIdx idx) {
    return sectionHeaders[raw(idx)];
  }

  std::span<const uint8_t> span() const {
    return std::span<const uint8_t>{
      reinterpret_cast<const uint8_t*>(this),
      sizeof(*this)
    };
  }

  Elf64_Ehdr fileHeader;
  std::array<Elf64_Phdr, raw(SegmentIdx::Total)> segmentHeaders;
  std::array<Elf64_Shdr, raw(SectionIdx::Total)> sectionHeaders;

 private:
  void initFileHeader() {
    memset(&fileHeader, 0, sizeof(fileHeader));

    fileHeader.e_ident[EI_MAG0] = ELFMAG0;
    fileHeader.e_ident[EI_MAG1] = ELFMAG1;
    fileHeader.e_ident[EI_MAG2] = ELFMAG2;
    fileHeader.e_ident[EI_MAG3] = ELFMAG3;

    fileHeader.e_ident[EI_CLASS] = ELFCLASS64;

    fileHeader.e_ident[EI_DATA] = ELFDATA2LSB;
    fileHeader.e_ident[EI_VERSION] = EV_CURRENT;
    fileHeader.e_ident[EI_OSABI] = ELFOSABI_LINUX;

    fileHeader.e_type = ET_DYN;
    fileHeader.e_machine = EM_X86_64;
    fileHeader.e_version = EV_CURRENT;

    fileHeader.e_phoff = offsetof(ElfHeaders, segmentHeaders);
    fileHeader.e_shoff = offsetof(ElfHeaders, sectionHeaders);
    fileHeader.e_ehsize = sizeof(fileHeader);
    fileHeader.e_phentsize = sizeof(Elf64_Phdr);
    fileHeader.e_phnum = segmentHeaders.size();
    fileHeader.e_shentsize = sizeof(Elf64_Shdr);
    fileHeader.e_shnum = sectionHeaders.size();
    fileHeader.e_shstrndx = raw(SectionIdx::Shstrtab);
  }
};

struct ElfObject {
  ElfObject() {
    initSymbols();
    initSections();
    initSegments();
  }

  ElfHeaders headers;
  ElfDynamicTable dynamics;
  ElfSymbolTable symbols;
  ElfStringTable symbolNames;
  ElfStringTable sectionNames;

 private:
  void initSymbols() {
    Elf64_Sym sym;
    memset(&sym, 0, sizeof(sym));
    sym.st_name = symbolNames.insert("collatz_conjecture");
    sym.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    sym.st_shndx = raw(SectionIdx::Text);
    sym.st_value = kTextStart;
    sym.st_size = kTextSize;

    symbols.insert(std::move(sym));
  }

  void initSections() {
    // Sections start right after the header table.
    uint32_t sectionOffset = sizeof(headers);

    // .text

    auto& text = headers.getSectionHeader(SectionIdx::Text);
    text.sh_name = sectionNames.insert(".text");
    text.sh_type = SHT_PROGBITS;
    text.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    text.sh_addr = kTextStart + sectionOffset;
    text.sh_offset = sectionOffset;
    text.sh_size = kTextSize;
    sectionOffset += text.sh_size;

    // .dynsym

    auto& dynsym = headers.getSectionHeader(SectionIdx::Dynsym);
    dynsym.sh_name = sectionNames.insert(".dynsym");
    dynsym.sh_type = SHT_DYNSYM;
    dynsym.sh_flags = SHF_ALLOC;
    dynsym.sh_addr = kDynsymStart + sectionOffset;
    dynsym.sh_offset = sectionOffset;
    dynsym.sh_size = symbols.size_bytes();
    dynsym.sh_link = raw(SectionIdx::Dynstr);
    // Index of the first non-null symbol.
    dynsym.sh_info = 1;
    dynsym.sh_entsize = sizeof(Elf64_Sym);
    sectionOffset += dynsym.sh_size;

    // .dynstr
    auto& dynstr = headers.getSectionHeader(SectionIdx::Dynstr);
    dynstr.sh_name = sectionNames.insert(".dynstr");
    dynstr.sh_type = SHT_STRTAB;
    dynstr.sh_flags = SHF_ALLOC;
    dynstr.sh_addr = dynsym.sh_addr + dynsym.sh_size;
    dynstr.sh_offset = sectionOffset;
    dynstr.sh_size = symbolNames.size_bytes();
    sectionOffset += dynstr.sh_size;

    // Set up dynamics.
    Elf64_Dyn dyn;
    dyn.d_tag = DT_STRTAB;
    dyn.d_un.d_ptr = dynstr.sh_addr;
    dynamics.insert(std::move(dyn));

    dyn.d_tag = DT_SYMTAB;
    dyn.d_un.d_ptr = dynsym.sh_addr;
    dynamics.insert(std::move(dyn));

    // .dynamic

    auto& dynamic = headers.getSectionHeader(SectionIdx::Dynamic);
    dynamic.sh_name = sectionNames.insert(".dynamic");
    dynamic.sh_type = SHT_DYNAMIC;
    dynamic.sh_flags = SHF_ALLOC | SHF_WRITE;
    dynamic.sh_addr = kDynamicStart;// + sectionOffset;
    dynamic.sh_offset = sectionOffset;
    dynamic.sh_size = dynamics.size_bytes();
    dynamic.sh_link = raw(SectionIdx::Dynstr);
    dynamic.sh_entsize = sizeof(Elf64_Dyn);
    sectionOffset += dynamic.sh_size;

    // .symtab

    auto& symtab = headers.getSectionHeader(SectionIdx::Symtab);
    symtab.sh_name = sectionNames.insert(".symtab");
    symtab.sh_type = SHT_SYMTAB;
    symtab.sh_offset = sectionOffset;
    symtab.sh_size = symbols.size_bytes();
    symtab.sh_link = raw(SectionIdx::Strtab);
    // Index of the first non-null symbol.
    symtab.sh_info = 1;
    symtab.sh_entsize = sizeof(Elf64_Sym);
    sectionOffset += symtab.sh_size;

    // .strtab

    auto& strtab = headers.getSectionHeader(SectionIdx::Strtab);
    strtab.sh_name = sectionNames.insert(".strtab");
    strtab.sh_type = SHT_STRTAB;
    strtab.sh_offset = sectionOffset;
    strtab.sh_size = symbolNames.size_bytes();
    sectionOffset += strtab.sh_size;

    // .shstrtab

    auto& shstrtab = headers.getSectionHeader(SectionIdx::Shstrtab);
    shstrtab.sh_name = sectionNames.insert(".shstrtab");
    shstrtab.sh_type = SHT_STRTAB;
    shstrtab.sh_offset = sectionOffset;
    shstrtab.sh_size = sectionNames.size_bytes();
    sectionOffset += shstrtab.sh_size;
  }

  void initSegments() {
    // Readable and executable segment for .text
    auto& text_section = headers.getSectionHeader(SectionIdx::Text);

    auto& text = headers.getSegmentHeader(SegmentIdx::Text);
    text.p_type = PT_LOAD;
    text.p_flags = PF_R | PF_X;
    text.p_offset = text_section.sh_offset;
    text.p_vaddr = text_section.sh_addr;
    text.p_filesz = text_section.sh_size;
    text.p_memsz = text.p_filesz;
    text.p_align = kTextAlign;

    // Readable segment for .dynsym and .dynstr
    auto& dynsym_section = headers.getSectionHeader(SectionIdx::Dynsym);
    auto& dynstr_section = headers.getSectionHeader(SectionIdx::Dynstr);

    auto& dynsym = headers.getSegmentHeader(SegmentIdx::Dynsym);
    dynsym.p_type = PT_LOAD;
    dynsym.p_flags = PF_R;
    dynsym.p_offset = dynsym_section.sh_offset;
    dynsym.p_vaddr = dynsym_section.sh_addr;
    dynsym.p_filesz = dynsym_section.sh_size + dynstr_section.sh_size;
    dynsym.p_memsz = dynsym.p_filesz;

#ifdef DYNAMIC_FIXED
    // FIXME: This leads to a SIGSEGV in dlopen() as it is.

    // Readable and writable segment for .dynamic
    auto& dynamic_section = headers.getSectionHeader(SectionIdx::Dynamic);

    auto& dynamic = headers.getSegmentHeader(SegmentIdx::Dynamic);
    dynamic.p_type = PT_DYNAMIC;
    dynamic.p_flags = PF_R | PF_W;
    dynamic.p_offset = dynamic_section.sh_offset;
    dynamic.p_vaddr = dynamic_section.sh_addr;
    dynamic.p_filesz = dynamic_section.sh_size;
    dynamic.p_memsz = dynamic.p_filesz;
#endif
  }
};

template <class T>
void write(std::ostream& os, T* data, size_t size) {
  os.write(reinterpret_cast<const char*>(data), size);
  assert(!os.bad());
}

template <class T>
void write(std::ostream& os, std::span<T> buf) {
  write(os, buf.data(), buf.size_bytes());
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::println(
      stderr,
      "Error: Takes exactly one argument (the path to write the shared object)"
    );
    return 1;
  }

  auto outPath = argv[1];

  ElfObject elf;

  std::ofstream out{outPath};

  // All the headers (file, program, sections).
  write(out, elf.headers.span());

  // .text
  write(out, collatz_conjecture, kTextSize);
  // .dynsym
  write(out, elf.symbols.span());
  // .dynstr
  write(out, elf.symbolNames.span());
  // .dynamic
  write(out, elf.dynamics.span());
  // .symtab
  write(out, elf.symbols.span());
  // .strtab
  write(out, elf.symbolNames.span());
  // .shstrtab
  write(out, elf.sectionNames.span());

  std::filesystem::permissions(
    outPath,
    std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
    std::filesystem::perm_options::add
  );

  return 0;
}
