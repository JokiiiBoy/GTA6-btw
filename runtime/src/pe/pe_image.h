#pragma once

// Portable PE image mapping groundwork (Phase 1/3 Linux native runtime).
// Maps sections and applies base relocations into memory using rt::os.
// It does NOT bind imports, install TLS, register unwind, or execute code.

#include <cstddef>
#include <cstdint>
#include <string>

#include "pe_reader.h"

namespace rt::pe {

struct MappedImage {
    uint8_t* base = nullptr;
    size_t size = 0;
    uint64_t preferred_base = 0;
    uint64_t actual_base = 0;
    bool relocated = false;
    std::string error;

    MappedImage() = default;
    MappedImage(const MappedImage&) = delete;
    MappedImage& operator=(const MappedImage&) = delete;
    MappedImage(MappedImage&& o) noexcept;
    MappedImage& operator=(MappedImage&& o) noexcept;
    ~MappedImage();

    explicit operator bool() const { return base != nullptr && error.empty(); }
};

// Maps a parsed PE file into memory. On success, sections are copied and base
// relocations are applied when the image cannot be loaded at its preferred base.
// The image is mapped RW; per-section protections can be applied later.
MappedImage map_pe(const PeFile& pe, bool apply_section_protection = false);

// Apply per-section protection flags from PE characteristics. Returns false on failure.
bool protect_pe_sections(const PeFile& pe, uint8_t* base, size_t size);

}  // namespace rt::pe
