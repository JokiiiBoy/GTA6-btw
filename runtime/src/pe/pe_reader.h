#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rt::pe {

struct DataDir {
    uint32_t rva = 0;
    uint32_t size = 0;
};

struct Section {
    std::string name;
    uint32_t virtual_address = 0;
    uint32_t virtual_size = 0;
    uint32_t raw_ptr = 0;
    uint32_t raw_size = 0;
    uint32_t characteristics = 0;
};

struct ImportEntry {
    std::string dll;
    std::string name;      // empty if ordinal
    uint16_t ordinal = 0;
    bool by_ordinal = false;
};

struct ExportEntry {
    std::string name;
    uint16_t ordinal = 0;  // biased ordinal
    uint32_t rva = 0;
    std::string forwarder; // non-empty if forwarder
};

struct ExportInfo {
    std::string name;
    uint32_t functions = 0;
    uint32_t names = 0;
    std::vector<ExportEntry> entries;
};

struct RelocSummary {
    bool present = false;
    uint32_t blocks = 0;
    uint32_t entries = 0;
};

struct PeFile {
    bool ok = false;
    std::string error;
    std::vector<uint8_t> buf;

    uint16_t machine = 0;
    uint16_t sections_count = 0;
    uint16_t characteristics = 0;
    bool pe32plus = false;
    uint32_t entry_rva = 0;
    uint64_t image_base = 0;
    uint32_t size_of_image = 0;
    uint32_t size_of_headers = 0;
    uint16_t subsystem = 0;
    uint16_t dll_characteristics = 0;
    uint32_t num_data_dirs = 0;

    std::vector<DataDir> data_dirs;
    std::vector<Section> sections;
    std::vector<ImportEntry> imports;
    ExportInfo exports;
    bool has_exports = false;
    RelocSummary relocs;
};

PeFile load_pe_file(const std::string& path);

}  // namespace rt::pe
