#include "pe_image.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "../os/os.h"

namespace rt::pe {
namespace {

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
            if (delta >= s.raw_size) return static_cast<size_t>(-1);  // in BSS/virtual tail
            const size_t off = static_cast<size_t>(s.raw_ptr) + delta;
            if (off < pe.buf.size()) return off;
            return static_cast<size_t>(-1);
        }
    }
    if (rva < pe.buf.size()) return rva;
    return static_cast<size_t>(-1);
}

os::VmProt section_prot(uint32_t c) {
    const bool r = (c & 0x40000000u) != 0;  // IMAGE_SCN_MEM_READ
    const bool w = (c & 0x80000000u) != 0;  // IMAGE_SCN_MEM_WRITE
    const bool x = (c & 0x20000000u) != 0;  // IMAGE_SCN_MEM_EXECUTE
    os::VmProt p = os::VmProt::None;
    if (r) p = p | os::VmProt::Read;
    if (w) p = p | os::VmProt::Write;
    if (x) p = p | os::VmProt::Exec;
    if (!r && !w && !x) p = os::VmProt::Read;
    return p;
}

bool apply_relocations(const PeFile& pe, uint8_t* base, size_t size, int64_t delta) {
    if (pe.data_dirs.size() <= 5 || pe.data_dirs[5].rva == 0 || pe.data_dirs[5].size == 0) return true;
    const uint32_t dir_rva = pe.data_dirs[5].rva;
    const uint32_t dir_size = pe.data_dirs[5].size;
    uint32_t off = 0;
    while (off + 8 <= dir_size) {
        const size_t block_off = rva_to_off(pe, dir_rva + off);
        if (block_off == static_cast<size_t>(-1)) return false;
        auto* page = at<uint32_t>(pe, block_off);
        auto* block_size = at<uint32_t>(pe, block_off + 4);
        if (!page || !block_size || *block_size < 8 || *block_size > dir_size - off) return false;
        const uint32_t count = (*block_size - 8) / 2;
        for (uint32_t i = 0; i < count; ++i) {
            auto* entry = at<uint16_t>(pe, block_off + 8 + i * 2);
            if (!entry) return false;
            const uint16_t type = (*entry >> 12) & 0xF;
            const uint16_t val = *entry & 0x0FFF;
            if (type == 0) continue;  // ABSOLUTE
            const uint32_t target_rva = *page + val;
            if (static_cast<size_t>(target_rva) >= size) return false;
            if (type == 10) {  // DIR64
                if (static_cast<size_t>(target_rva) + 8 > size) return false;
                auto* p = reinterpret_cast<uint64_t*>(base + target_rva);
                *p = static_cast<uint64_t>(static_cast<int64_t>(*p) + delta);
            } else if (type == 3) {  // HIGHLOW
                if (static_cast<size_t>(target_rva) + 4 > size) return false;
                auto* p = reinterpret_cast<uint32_t*>(base + target_rva);
                *p = static_cast<uint32_t>(static_cast<int32_t>(*p) + static_cast<int32_t>(delta));
            } else {
                return false;  // unsupported relocation type in groundwork mapper
            }
        }
        off += *block_size;
    }
    return true;
}

}  // namespace

MappedImage::MappedImage(MappedImage&& o) noexcept
    : base(o.base), size(o.size), preferred_base(o.preferred_base), actual_base(o.actual_base),
      relocated(o.relocated), error(std::move(o.error)) {
    o.base = nullptr;
    o.size = 0;
}

MappedImage& MappedImage::operator=(MappedImage&& o) noexcept {
    if (this != &o) {
        if (base) os::vm_release(base, size);
        base = o.base;
        size = o.size;
        preferred_base = o.preferred_base;
        actual_base = o.actual_base;
        relocated = o.relocated;
        error = std::move(o.error);
        o.base = nullptr;
        o.size = 0;
    }
    return *this;
}

MappedImage::~MappedImage() {
    if (base) os::vm_release(base, size);
}

bool protect_pe_sections(const PeFile& pe, uint8_t* base, size_t size) {
    if (!base || size == 0) return false;
    // Headers read-only (best effort).
    const size_t hdr = std::min<size_t>(pe.size_of_headers, size);
    if (hdr) os::vm_protect(base, hdr, os::VmProt::Read);
    for (const auto& s : pe.sections) {
        const size_t span = std::max(s.virtual_size, s.raw_size);
        if (span == 0) continue;
        if (static_cast<size_t>(s.virtual_address) + span > size) return false;
        if (!os::vm_protect(base + s.virtual_address, span, section_prot(s.characteristics))) return false;
    }
    return true;
}

MappedImage map_pe(const PeFile& pe, bool apply_section_protection) {
    MappedImage out;
    if (!pe.ok) {
        out.error = "invalid PE";
        return out;
    }
    if (pe.size_of_image == 0) {
        out.error = "zero SizeOfImage";
        return out;
    }
    out.preferred_base = pe.image_base;
    out.size = pe.size_of_image;

    void* mem = os::vm_reserve(reinterpret_cast<void*>(static_cast<uintptr_t>(pe.image_base)), pe.size_of_image, false);
    if (!mem) mem = os::vm_reserve(nullptr, pe.size_of_image, false);
    if (!mem) {
        out.error = "reserve failed";
        out.size = 0;
        return out;
    }
    out.base = static_cast<uint8_t*>(mem);
    out.actual_base = reinterpret_cast<uint64_t>(out.base);

    if (!os::vm_commit(out.base, out.size, os::VmProt::Read | os::VmProt::Write)) {
        out.error = "commit failed";
        os::vm_release(out.base, out.size);
        out.base = nullptr;
        return out;
    }

    std::memset(out.base, 0, out.size);
    const size_t headers = std::min<size_t>(pe.size_of_headers, pe.buf.size());
    if (headers) std::memcpy(out.base, pe.buf.data(), headers);

    for (const auto& s : pe.sections) {
        if (s.raw_size == 0) continue;
        if (static_cast<size_t>(s.virtual_address) + s.raw_size > out.size) {
            out.error = "section out of bounds: " + s.name +
                        " va=" + std::to_string(s.virtual_address) +
                        " raw=" + std::to_string(s.raw_size) +
                        " img=" + std::to_string(out.size);
            os::vm_release(out.base, out.size);
            out.base = nullptr;
            return out;
        }
        if (static_cast<size_t>(s.raw_ptr) + s.raw_size > pe.buf.size()) {
            out.error = "section raw out of bounds";
            os::vm_release(out.base, out.size);
            out.base = nullptr;
            return out;
        }
        std::memcpy(out.base + s.virtual_address, pe.buf.data() + s.raw_ptr, s.raw_size);
    }

    const int64_t delta = static_cast<int64_t>(out.actual_base) - static_cast<int64_t>(out.preferred_base);
    if (delta != 0) {
        out.relocated = true;
        if (!apply_relocations(pe, out.base, out.size, delta)) {
            out.error = "relocation failed";
            os::vm_release(out.base, out.size);
            out.base = nullptr;
            return out;
        }
    }

    if (apply_section_protection && !protect_pe_sections(pe, out.base, out.size)) {
        out.error = "section protection failed";
        os::vm_release(out.base, out.size);
        out.base = nullptr;
        return out;
    }

    return out;
}

}  // namespace rt::pe
