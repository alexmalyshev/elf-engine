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

  constexpr std::span<const std::byte> bytes() const {
    return std::as_bytes(std::span{this, 1});
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

  ElfDynamicTable dynamic;
  ElfSymbolTable dynsym;
  ElfStringTable dynstr;
  ElfSymbolTable symtab;
  ElfStringTable strtab;
  ElfStringTable shstrtab;

  uint32_t sectionOffset{0};

 private:
  void initSymbols() {
    Elf64_Sym sym;
    memset(&sym, 0, sizeof(sym));
    sym.st_name = strtab.insert("collatz_conjecture");
    sym.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    sym.st_shndx = raw(SectionIdx::Text);
    sym.st_value = kTextStart;
    sym.st_size = kTextSize;

    symtab.insert(std::move(sym));

    dynsym = symtab;
    dynstr = strtab;
  }

  void initTextSection() {
    auto& section = headers.getSectionHeader(SectionIdx::Text);
    section.sh_name = shstrtab.insert(".text");
    section.sh_type = SHT_PROGBITS;
    section.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    section.sh_addr = kTextStart + sectionOffset;
    section.sh_offset = sectionOffset;
    section.sh_size = kTextSize;
    sectionOffset += section.sh_size;
  }

  void initDynsymSection() {
    auto& section = headers.getSectionHeader(SectionIdx::Dynsym);
    section.sh_name = shstrtab.insert(".dynsym");
    section.sh_type = SHT_DYNSYM;
    section.sh_flags = SHF_ALLOC;
    section.sh_addr = kDynsymStart + sectionOffset;
    section.sh_offset = sectionOffset;
    section.sh_size = dynsym.bytes().size();
    section.sh_link = raw(SectionIdx::Dynstr);
    // Index of the first non-null symbol.
    section.sh_info = 1;
    section.sh_entsize = sizeof(Elf64_Sym);
    sectionOffset += section.sh_size;
  }

  void initDynstrSection() {
    auto& dynsymSection = headers.getSectionHeader(SectionIdx::Dynsym);

    auto& section = headers.getSectionHeader(SectionIdx::Dynstr);
    section.sh_name = shstrtab.insert(".dynstr");
    section.sh_type = SHT_STRTAB;
    section.sh_flags = SHF_ALLOC;
    section.sh_addr = dynsymSection.sh_addr + dynsymSection.sh_size;
    section.sh_offset = sectionOffset;
    section.sh_size = dynstr.bytes().size();
    sectionOffset += section.sh_size;
  }

  void initDynamicSection() {
    auto& dynsymSection = headers.getSectionHeader(SectionIdx::Dynsym);
    auto& dynstrSection = headers.getSectionHeader(SectionIdx::Dynstr);

    // Set up dynamic table now that .dynsym and .dynstr are initialized.
    Elf64_Dyn dyn;
    dyn.d_tag = DT_STRTAB;
    dyn.d_un.d_ptr = dynstrSection.sh_addr;
    dynamic.insert(std::move(dyn));

    dyn.d_tag = DT_SYMTAB;
    dyn.d_un.d_ptr = dynsymSection.sh_addr;
    dynamic.insert(std::move(dyn));

    auto& section = headers.getSectionHeader(SectionIdx::Dynamic);
    section.sh_name = shstrtab.insert(".dynamic");
    section.sh_type = SHT_DYNAMIC;
    section.sh_flags = SHF_ALLOC | SHF_WRITE;
    section.sh_addr = kDynamicStart + sectionOffset;
    section.sh_offset = sectionOffset;
    section.sh_size = dynamic.bytes().size();
    section.sh_link = raw(SectionIdx::Dynstr);
    section.sh_entsize = sizeof(Elf64_Dyn);
    sectionOffset += section.sh_size;

    Elf64_Sym sym;
    memset(&sym, 0, sizeof(sym));
    sym.st_name = strtab.insert("_DYNAMIC");
    sym.st_info = ELF64_ST_INFO(STB_LOCAL, STT_OBJECT);
    sym.st_shndx = raw(SectionIdx::Dynamic);
    sym.st_value = section.sh_addr;
    sym.st_size = section.sh_size;

    symtab.insert(std::move(sym));
  }

  void initSymtabSection() {
    auto& section = headers.getSectionHeader(SectionIdx::Symtab);
    section.sh_name = shstrtab.insert(".symtab");
    section.sh_type = SHT_SYMTAB;
    section.sh_offset = sectionOffset;
    section.sh_size = symtab.bytes().size();
    section.sh_link = raw(SectionIdx::Strtab);
    // Index of the first non-null symbol.
    section.sh_info = 1;
    section.sh_entsize = sizeof(Elf64_Sym);
    sectionOffset += section.sh_size;
  }

  void initStrtabSection() {
    auto& section = headers.getSectionHeader(SectionIdx::Strtab);
    section.sh_name = shstrtab.insert(".strtab");
    section.sh_type = SHT_STRTAB;
    section.sh_offset = sectionOffset;
    section.sh_size = strtab.bytes().size();
    sectionOffset += section.sh_size;
  }

  void initShstrtabSection() {
    auto& section = headers.getSectionHeader(SectionIdx::Shstrtab);
    section.sh_name = shstrtab.insert(".shstrtab");
    section.sh_type = SHT_STRTAB;
    section.sh_offset = sectionOffset;
    section.sh_size = shstrtab.bytes().size();
    sectionOffset += section.sh_size;
  }

  void initSections() {
    // Sections start right after the header table.
    sectionOffset = sizeof(headers);

    // The order here must match that of SectionIdx.

    initTextSection();
    initDynsymSection();
    initDynstrSection();
    initDynamicSection();
    initSymtabSection();
    initStrtabSection();
    initShstrtabSection();
  }

  void initTextSegment() {
    // .text is readable and executable.
    auto& section = headers.getSectionHeader(SectionIdx::Text);

    auto& segment = headers.getSegmentHeader(SegmentIdx::Text);
    segment.p_type = PT_LOAD;
    segment.p_flags = PF_R | PF_X;
    segment.p_offset = section.sh_offset;
    segment.p_vaddr = section.sh_addr;
    segment.p_filesz = section.sh_size;
    segment.p_memsz = segment.p_filesz;
    segment.p_align = kTextAlign;
  }

  void initReadonlySegment() {
    // .dynsym and .dynstr are in a readonly segment.
    auto& dynsym = headers.getSectionHeader(SectionIdx::Dynsym);
    auto& dynstr = headers.getSectionHeader(SectionIdx::Dynstr);

    // Below expects this.
    assert(dynsym.sh_addr < dynstr.sh_addr);

    auto& segment = headers.getSegmentHeader(SegmentIdx::Dynsym);
    segment.p_type = PT_LOAD;
    segment.p_flags = PF_R;
    segment.p_offset = dynsym.sh_offset;
    segment.p_vaddr = dynsym.sh_addr;
    segment.p_filesz = dynsym.sh_size + dynstr.sh_size;
    segment.p_memsz = segment.p_filesz;
  }

  void initDynamicSegment() {
#ifdef DYNAMIC_FIXED
    // FIXME: This leads to a SIGSEGV in dlopen() as it is.

    // Readable and writable segment for .dynamic
    auto& section = headers.getSectionHeader(SectionIdx::Dynamic);

    auto& segment = headers.getSegmentHeader(SegmentIdx::Dynamic);
    segment.p_type = PT_DYNAMIC;
    segment.p_flags = PF_R | PF_W;
    segment.p_offset = section.sh_offset;
    segment.p_vaddr = section.sh_addr;
    segment.p_filesz = section.sh_size;
    segment.p_memsz = segment.p_filesz;
#endif
  }

  void initSegments() {
    initTextSegment();
    initReadonlySegment();
    initDynamicSegment();
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
  write(out, elf.headers.bytes());

  // .text
  write(out, collatz_conjecture, kTextSize);
  // .dynsym
  write(out, elf.dynsym.bytes());
  // .dynstr
  write(out, elf.dynstr.bytes());
  // .dynamic
  write(out, elf.dynamic.bytes());
  // .symtab
  write(out, elf.symtab.bytes());
  // .strtab
  write(out, elf.strtab.bytes());
  // .shstrtab
  write(out, elf.shstrtab.bytes());

  std::filesystem::permissions(
    outPath,
    std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
    std::filesystem::perm_options::add
  );

  return 0;
}
