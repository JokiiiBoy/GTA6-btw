#include "pe_reader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace rt::pe {
namespace {

#pragma pack(push, 1)
struct DosHeader {
    uint16_t e_magic;
    uint16_t e_cblp, e_cp, e_crlc, e_cparhdr, e_minalloc, e_maxalloc, e_ss, e_sp;
    uint16_t e_csum, e_ip, e_cs, e_lfarlc, e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid, e_oeminfo;
    uint16_t e_res2[10];
    int32_t e_lfanew;
};
struct FileHeaderRaw {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};
struct DataDirRaw { uint32_t VirtualAddress; uint32_t Size; };
struct OptionalHeader64Raw {
    uint16_t Magic;
    uint8_t MajorLinkerVersion, MinorLinkerVersion;
    uint32_t SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint, BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment, FileAlignment;
    uint16_t MajorOperatingSystemVersion, MinorOperatingSystemVersion;
    uint16_t MajorImageVersion, MinorImageVersion;
    uint16_t MajorSubsystemVersion, MinorSubsystemVersion;
    uint32_t Win32VersionValue, SizeOfImage, SizeOfHeaders, CheckSum;
    uint16_t Subsystem, DllCharacteristics;
    uint64_t SizeOfStackReserve, SizeOfStackCommit, SizeOfHeapReserve, SizeOfHeapCommit;
    uint32_t LoaderFlags, NumberOfRvaAndSizes;
    DataDirRaw DataDirectories[16];
};
struct SectionHeaderRaw {
    char Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
};
struct ImportDescriptorRaw {
    uint32_t OriginalFirstThunk;
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;
    uint32_t FirstThunk;
};
struct ExportDirectoryRaw {
    uint32_t Characteristics;
    uint32_t TimeDateStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint32_t Name;
    uint32_t Base;
    uint32_t NumberOfFunctions;
    uint32_t NumberOfNames;
    uint32_t AddressOfFunctions;
    uint32_t AddressOfNames;
    uint32_t AddressOfNameOrdinals;
};
#pragma pack(pop)

template <typename T>
const T* at(const PeFile& pe, size_t off) {
    if (off + sizeof(T) > pe.buf.size()) return nullptr;
    return reinterpret_cast<const T*>(pe.buf.data() + off);
}

size_t rva_to_off(const PeFile& pe, uint32_t rva) {
    for (const auto& s : pe.sections) {
        const uint32_t span = std::max(s.virtual_size, s.raw_size);
        if (rva >= s.virtual_address && rva < s.virtual_address + span) {
            const uint32_t delta = rva - s.virtual_address;
            if (delta >= s.raw_size) return static_cast<size_t>(-1);
            const size_t off = static_cast<size_t>(s.raw_ptr) + delta;
            if (off < pe.buf.size()) return off;
        }
    }
    return static_cast<size_t>(-1);
}

const char* cstr_rva(const PeFile& pe, uint32_t rva) {
    const size_t off = rva_to_off(pe, rva);
    if (off == static_cast<size_t>(-1) || off >= pe.buf.size()) return nullptr;
    const size_t end = std::min(pe.buf.size(), off + 4096);
    for (size_t i = off; i < end; ++i)
        if (pe.buf[i] == 0) return reinterpret_cast<const char*>(pe.buf.data() + off);
    return nullptr;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0) return false;
    out.resize(static_cast<size_t>(n));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(out.data()), n);
    return static_cast<size_t>(f.gcount()) == out.size();
}

void parse_imports(PeFile& pe) {
    if (pe.data_dirs.size() <= 1 || pe.data_dirs[1].rva == 0) return;
    size_t off = rva_to_off(pe, pe.data_dirs[1].rva);
    if (off == static_cast<size_t>(-1)) return;
    size_t desc_count = 0;
    for (;;) {
        auto* d = at<ImportDescriptorRaw>(pe, off);
        if (!d) break;
        if (d->OriginalFirstThunk == 0 && d->TimeDateStamp == 0 && d->ForwarderChain == 0 && d->Name == 0 &&
            d->FirstThunk == 0)
            break;
        const char* dll = cstr_rva(pe, d->Name);
        const std::string dll_name = dll ? dll : "<bad>";
        const uint32_t thunk_rva = d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk;
        size_t toff = rva_to_off(pe, thunk_rva);
        if (toff != static_cast<size_t>(-1)) {
            size_t n = 0;
            while (n < 32768) {
                if (pe.pe32plus) {
                    auto* v = at<uint64_t>(pe, toff);
                    if (!v || *v == 0) break;
                    ImportEntry e;
                    e.dll = dll_name;
                    const uint64_t val = *v;
                    if (val & (1ull << 63)) {
                        e.by_ordinal = true;
                        e.ordinal = static_cast<uint16_t>(val & 0xffff);
                    } else {
                        const size_t hn = rva_to_off(pe, static_cast<uint32_t>(val));
                        auto* hint_name = at<uint8_t>(pe, hn);
                        if (hn != static_cast<size_t>(-1) && hint_name && hn + 2 < pe.buf.size()) {
                            e.ordinal = *reinterpret_cast<const uint16_t*>(pe.buf.data() + hn);
                            e.name = reinterpret_cast<const char*>(pe.buf.data() + hn + 2);
                        } else {
                            e.name = "<bad>";
                        }
                    }
                    pe.imports.push_back(std::move(e));
                    toff += 8;
                } else {
                    auto* v = at<uint32_t>(pe, toff);
                    if (!v || *v == 0) break;
                    ImportEntry e;
                    e.dll = dll_name;
                    const uint32_t val = *v;
                    if (val & 0x80000000u) {
                        e.by_ordinal = true;
                        e.ordinal = static_cast<uint16_t>(val & 0xffff);
                    } else {
                        const size_t hn = rva_to_off(pe, val);
                        if (hn != static_cast<size_t>(-1) && hn + 2 < pe.buf.size()) {
                            e.ordinal = *reinterpret_cast<const uint16_t*>(pe.buf.data() + hn);
                            e.name = reinterpret_cast<const char*>(pe.buf.data() + hn + 2);
                        } else {
                            e.name = "<bad>";
                        }
                    }
                    pe.imports.push_back(std::move(e));
                    toff += 4;
                }
                ++n;
            }
        }
        off += sizeof(ImportDescriptorRaw);
        if (++desc_count > 4096) break;
    }
}

void parse_exports(PeFile& pe) {
    if (pe.data_dirs.empty() || pe.data_dirs[0].rva == 0) return;
    const uint32_t dir_rva = pe.data_dirs[0].rva;
    const uint32_t dir_size = pe.data_dirs[0].size;
    auto* e = at<ExportDirectoryRaw>(pe, rva_to_off(pe, dir_rva));
    if (!e) return;
    pe.has_exports = true;
    const char* name = cstr_rva(pe, e->Name);
    pe.exports.name = name ? name : "";
    pe.exports.functions = e->NumberOfFunctions;
    pe.exports.names = e->NumberOfNames;

    auto in_export_dir = [&](uint32_t rva) { return dir_size && rva >= dir_rva && rva < dir_rva + dir_size; };

    std::vector<uint32_t> func_rvas(e->NumberOfFunctions);
    for (uint32_t i = 0; i < e->NumberOfFunctions; ++i) {
        auto* p = at<uint32_t>(pe, rva_to_off(pe, e->AddressOfFunctions + i * 4));
        func_rvas[i] = p ? *p : 0;
    }

    std::vector<uint16_t> ordinals(e->NumberOfNames);
    std::vector<uint32_t> name_rvas(e->NumberOfNames);
    for (uint32_t i = 0; i < e->NumberOfNames; ++i) {
        auto* o = at<uint16_t>(pe, rva_to_off(pe, e->AddressOfNameOrdinals + i * 2));
        auto* n = at<uint32_t>(pe, rva_to_off(pe, e->AddressOfNames + i * 4));
        ordinals[i] = o ? *o : 0;
        name_rvas[i] = n ? *n : 0;
    }

    pe.exports.entries.reserve(e->NumberOfFunctions);
    for (uint32_t i = 0; i < e->NumberOfFunctions; ++i) {
        const uint32_t rva = func_rvas[i];
        if (rva == 0) continue;
        ExportEntry ent;
        ent.ordinal = static_cast<uint16_t>(e->Base + i);
        ent.rva = rva;
        if (in_export_dir(rva)) {
            const char* f = cstr_rva(pe, rva);
            if (f) ent.forwarder = f;
        }
        pe.exports.entries.push_back(std::move(ent));
    }

    for (uint32_t i = 0; i < e->NumberOfNames; ++i) {
        const uint16_t idx = ordinals[i];
        if (idx >= func_rvas.size()) continue;
        const uint32_t rva = func_rvas[idx];
        const char* nm = cstr_rva(pe, name_rvas[i]);
        // find existing entry by ordinal
        const uint16_t ord = static_cast<uint16_t>(e->Base + idx);
        bool found = false;
        for (auto& ent : pe.exports.entries) {
            if (ent.ordinal == ord) {
                if (nm) ent.name = nm;
                if (ent.rva == 0) ent.rva = rva;
                found = true;
                break;
            }
        }
        if (!found) {
            ExportEntry ent;
            ent.ordinal = ord;
            ent.rva = rva;
            if (nm) ent.name = nm;
            if (in_export_dir(rva)) { const char* f = cstr_rva(pe, rva); if (f) ent.forwarder = f; }
            pe.exports.entries.push_back(std::move(ent));
        }
    }
}

void parse_relocs(PeFile& pe) {
    if (pe.data_dirs.size() <= 5 || pe.data_dirs[5].rva == 0) return;
    pe.relocs.present = true;
    size_t off = rva_to_off(pe, pe.data_dirs[5].rva);
    uint32_t remaining = pe.data_dirs[5].size;
    while (remaining >= 8 && off != static_cast<size_t>(-1)) {
        auto* page = at<uint32_t>(pe, off);
        auto* size = at<uint32_t>(pe, off + 4);
        if (!page || !size || *size < 8) break;
        pe.relocs.blocks++;
        pe.relocs.entries += (*size - 8) / 2;
        off += *size;
        remaining = (remaining > *size) ? remaining - *size : 0;
        if (pe.relocs.blocks > 100000) break;
    }
}

}  // namespace

PeFile load_pe_file(const std::string& path) {
    PeFile pe;
    if (!read_file(path, pe.buf)) {
        pe.error = "cannot open file";
        return pe;
    }
    if (pe.buf.size() < sizeof(DosHeader)) {
        pe.error = "too small";
        return pe;
    }
    auto* dos = reinterpret_cast<const DosHeader*>(pe.buf.data());
    if (dos->e_magic != 0x5A4D || dos->e_lfanew < 0) {
        pe.error = "missing MZ";
        return pe;
    }
    const size_t nt_off = static_cast<size_t>(dos->e_lfanew);
    if (nt_off + 4 + sizeof(FileHeaderRaw) > pe.buf.size()) {
        pe.error = "truncated NT headers";
        return pe;
    }
    const uint32_t sig = *reinterpret_cast<const uint32_t*>(pe.buf.data() + nt_off);
    if (sig != 0x00004550) {
        pe.error = "missing PE signature";
        return pe;
    }
    auto* fh = reinterpret_cast<const FileHeaderRaw*>(pe.buf.data() + nt_off + 4);
    pe.machine = fh->Machine;
    pe.sections_count = fh->NumberOfSections;
    pe.characteristics = fh->Characteristics;
    const uint8_t* opt = pe.buf.data() + nt_off + 4 + sizeof(FileHeaderRaw);
    const size_t opt_size = fh->SizeOfOptionalHeader;
    if (nt_off + 4 + sizeof(FileHeaderRaw) + opt_size > pe.buf.size()) {
        pe.error = "truncated optional header";
        return pe;
    }
    if (opt_size < 2) {
        pe.error = "optional header too small";
        return pe;
    }
    const uint16_t magic = *reinterpret_cast<const uint16_t*>(opt);
    pe.pe32plus = magic == 0x20b;
    const bool pe32 = magic == 0x10b;
    if (!pe.pe32plus && !pe32) {
        pe.error = "unsupported optional header magic";
        return pe;
    }
    if (pe.pe32plus && opt_size >= 112) {
        // Read fixed PE32+ optional header fields; data directories may be fewer than 16.
        auto read_u16 = [&](size_t o) { uint16_t v; std::memcpy(&v, opt + o, 2); return v; };
        auto read_u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, opt + o, 4); return v; };
        auto read_u64 = [&](size_t o) { uint64_t v; std::memcpy(&v, opt + o, 8); return v; };
        pe.entry_rva = read_u32(16);
        pe.image_base = read_u64(24);
        pe.size_of_image = read_u32(56);
        pe.size_of_headers = read_u32(60);
        pe.subsystem = read_u16(68);
        pe.dll_characteristics = read_u16(70);
        pe.num_data_dirs = read_u32(108);
        const uint32_t dd_count = std::min<uint32_t>(pe.num_data_dirs, (opt_size >= 112) ? (static_cast<uint32_t>(opt_size) - 112) / 8 : 0);
        for (uint32_t i = 0; i < dd_count && i < 16; ++i) {
            DataDir d{};
            d.rva = read_u32(112 + i * 8);
            d.size = read_u32(112 + i * 8 + 4);
            pe.data_dirs.push_back(d);
        }
    } else if (pe32 && opt_size >= 96) {
        std::memcpy(&pe.entry_rva, opt + 16, 4);
        uint32_t base32 = 0;
        std::memcpy(&base32, opt + 28, 4);
        pe.image_base = base32;
        std::memcpy(&pe.size_of_image, opt + 56, 4);
        std::memcpy(&pe.size_of_headers, opt + 60, 4);
        std::memcpy(&pe.subsystem, opt + 68, 2);
        std::memcpy(&pe.dll_characteristics, opt + 70, 2);
        std::memcpy(&pe.num_data_dirs, opt + 92, 4);
        const uint32_t dd_count = std::min<uint32_t>(pe.num_data_dirs, (opt_size - 96) / 8);
        for (uint32_t i = 0; i < dd_count && i < 16; ++i) {
            DataDir d{};
            std::memcpy(&d.rva, opt + 96 + i * 8, 4);
            std::memcpy(&d.size, opt + 96 + i * 8 + 4, 4);
            pe.data_dirs.push_back(d);
        }
    } else {
        pe.error = "optional header too small for format";
        return pe;
    }

    const size_t sec_off = nt_off + 4 + sizeof(FileHeaderRaw) + opt_size;
    for (uint16_t i = 0; i < fh->NumberOfSections; ++i) {
        auto* sh = at<SectionHeaderRaw>(pe, sec_off + i * sizeof(SectionHeaderRaw));
        if (!sh) break;
        Section s;
        char name[9] = {};
        std::memcpy(name, sh->Name, 8);
        s.name = name;
        s.virtual_address = sh->VirtualAddress;
        s.virtual_size = sh->VirtualSize;
        s.raw_ptr = sh->PointerToRawData;
        s.raw_size = sh->SizeOfRawData;
        s.characteristics = sh->Characteristics;
        pe.sections.push_back(std::move(s));
    }

    parse_imports(pe);
    parse_exports(pe);
    parse_relocs(pe);
    pe.ok = true;
    return pe;
}

}  // namespace rt::pe
