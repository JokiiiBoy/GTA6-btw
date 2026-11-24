#include "../src/pe/pe_image.h"
#include "../src/pe/pe_reader.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)

namespace {

std::string tmp_path(const char* name) {
    return std::string("/tmp/") + name + "-" + std::to_string(::getpid()) + ".bin";
}

void put_u16(std::vector<uint8_t>& b, size_t off, uint16_t v) { std::memcpy(&b[off], &v, 2); }
void put_u32(std::vector<uint8_t>& b, size_t off, uint32_t v) { std::memcpy(&b[off], &v, 4); }
void put_u64(std::vector<uint8_t>& b, size_t off, uint64_t v) { std::memcpy(&b[off], &v, 8); }

// Builds a minimal PE64: 1 section .text, optional DIR64 relocation at RVA 0x1008
// pointing to a 64-bit value at RVA 0x1000 initialized to image_base+0x1000.
std::vector<uint8_t> make_min_pe64(bool with_reloc) {
    const uint32_t file_align = 0x200;
    const uint32_t sect_align = 0x1000;
    const uint32_t headers_size = 0x200;
    const uint32_t text_rva = 0x1000;
    const uint32_t text_raw = 0x200;
    const uint32_t reloc_rva = 0x2000;
    const uint32_t reloc_raw = 0x200;
    const uint32_t size_of_image = with_reloc ? 0x3000 : 0x2000;
    const uint64_t image_base = 0x0000000140000000ULL;

    std::vector<uint8_t> b(headers_size + text_raw + (with_reloc ? reloc_raw : 0), 0);
    // DOS
    put_u16(b, 0, 0x5A4D);
    put_u32(b, 0x3C, 0x80);
    // PE sig
    put_u32(b, 0x80, 0x00004550);
    // File header at 0x84
    put_u16(b, 0x84, 0x8664); // Machine x64
    put_u16(b, 0x86, with_reloc ? 2 : 1); // NumberOfSections
    const uint16_t opt_size = 0xA0; // 112 fixed + 6 data dirs * 8
    put_u16(b, 0x94, opt_size); // SizeOfOptionalHeader
    put_u16(b, 0x96, 0x22); // Characteristics (EXECUTABLE_IMAGE|LARGE_ADDRESS_AWARE)
    // Optional header PE32+ at 0x98
    const size_t oh = 0x98;
    put_u16(b, oh + 0, 0x20B); // Magic PE32+
    put_u32(b, oh + 4, text_raw); // SizeOfCode
    put_u32(b, oh + 16, text_rva); // EntryPoint
    put_u64(b, oh + 24, image_base);
    put_u32(b, oh + 32, sect_align);
    put_u32(b, oh + 36, file_align);
    put_u16(b, oh + 40, 6); put_u16(b, oh + 42, 0); // OS version
    put_u16(b, oh + 44, 0); put_u16(b, oh + 46, 0); // Image version
    put_u16(b, oh + 48, 6); put_u16(b, oh + 50, 0); // Subsystem version
    put_u32(b, oh + 56, size_of_image);
    put_u32(b, oh + 60, headers_size);
    put_u16(b, oh + 68, 3); // Subsystem CUI
    put_u16(b, oh + 70, 0); // DLL chars
    put_u64(b, oh + 72, 0x100000); // Stack reserve
    put_u64(b, oh + 80, 0x1000);   // Stack commit
    put_u64(b, oh + 88, 0x100000); // Heap reserve
    put_u64(b, oh + 96, 0x1000);   // Heap commit
    put_u32(b, oh + 104, 0);       // LoaderFlags
    put_u32(b, oh + 108, 6); // NumberOfRvaAndSizes
    // Data dirs at oh+112, 6 entries * 8 bytes
    if (with_reloc) {
        put_u32(b, oh + 112 + 5 * 8, reloc_rva); // Base reloc RVA
        put_u32(b, oh + 112 + 5 * 8 + 4, 12);    // Size (one block: 8 hdr + 2 entries incl pad)
    }
    // Section headers start at optional header + opt_size
    size_t sh = 0x98 + opt_size;
    std::memcpy(&b[sh], ".text\0\0", 8);
    put_u32(b, sh + 8, text_raw); // VirtualSize
    put_u32(b, sh + 12, text_rva);
    put_u32(b, sh + 16, text_raw); // RawSize
    put_u32(b, sh + 20, headers_size); // RawPtr
    put_u32(b, sh + 36, 0x60000020); // CODE|EXECUTE|READ

    // .text content
    size_t text_off = headers_size;
    // at RVA 0x1000: a 64-bit absolute value = image_base + 0x1000 (will be relocated)
    put_u64(b, text_off + 0, image_base + 0x1000);
    // at RVA 0x1008: some bytes
    b[text_off + 8] = 0xC3; // ret

    if (with_reloc) {
        // .reloc section header
        size_t sh2 = sh + 40;
        std::memcpy(&b[sh2], ".reloc\0", 8);
        put_u32(b, sh2 + 8, 0x1000);
        put_u32(b, sh2 + 12, reloc_rva);
        put_u32(b, sh2 + 16, reloc_raw);
        put_u32(b, sh2 + 20, headers_size + text_raw);
        put_u32(b, sh2 + 36, 0x42000040); // INITIALIZED_DATA|DISCARDABLE|READ

        // relocation block at reloc_raw
        size_t rb = headers_size + text_raw;
        put_u32(b, rb + 0, text_rva); // Page RVA
        put_u32(b, rb + 4, 12);       // Block size (8 + 2*2)
        put_u16(b, rb + 8, (10u << 12) | 0x000); // DIR64 at page+0
        put_u16(b, rb + 10, 0); // ABSOLUTE padding
    }
    (void)sect_align; (void)file_align;
    return b;
}

int test_map_no_reloc() {
    const std::string p = tmp_path("pe-image-noreloc");
    auto bytes = make_min_pe64(false);
    {
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    auto pe = rt::pe::load_pe_file(p);
    CHECK(pe.ok);
    CHECK(pe.pe32plus);
    auto img = rt::pe::map_pe(pe, false);
    CHECK(static_cast<bool>(img));
    CHECK(img.base != nullptr);
    CHECK(img.size == pe.size_of_image);
    // header magic
    CHECK(img.base[0] == 'M' && img.base[1] == 'Z');
    // mapped .text
    const auto* val = reinterpret_cast<const uint64_t*>(img.base + 0x1000);
    const uint64_t expected = 0x0000000140000000ULL + 0x1000 + (img.relocated ? (static_cast<int64_t>(img.actual_base) - static_cast<int64_t>(img.preferred_base)) : 0);
    CHECK(*val == expected);
    CHECK(img.base[0x1008] == 0xC3);
    ::unlink(p.c_str());
    return 0;
}

int test_map_with_reloc() {
    const std::string p = tmp_path("pe-image-reloc");
    auto bytes = make_min_pe64(true);
    {
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    auto pe = rt::pe::load_pe_file(p);
    CHECK(pe.ok);
    CHECK(pe.relocs.present);
    auto img = rt::pe::map_pe(pe, false);
    CHECK(static_cast<bool>(img));
    CHECK(img.base != nullptr);
    const auto* val = reinterpret_cast<const uint64_t*>(img.base + 0x1000);
    const uint64_t expected = static_cast<uint64_t>(static_cast<int64_t>(0x0000000140000000ULL + 0x1000) +
                                                    (static_cast<int64_t>(img.actual_base) - static_cast<int64_t>(img.preferred_base)));
    CHECK(*val == expected);
    ::unlink(p.c_str());
    return 0;
}

}  // namespace

int main() {
    if (test_map_no_reloc()) return 1;
    if (test_map_with_reloc()) return 1;
    std::printf("pe_image: map, sections, relocs\n");
    return 0;
}
