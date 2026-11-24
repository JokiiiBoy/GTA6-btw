// Minimal PE inspection tool (Phase 1 Linux runtime groundwork).
// Reads PE files without mapping or executing the image.
// No game data is embedded; it only reads files the user passes.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "pe/pe_reader.h"

namespace {

const char* machine_name(uint16_t m) {
    switch (m) {
        case 0x8664: return "x64";
        case 0x014c: return "x86";
        case 0xaa64: return "arm64";
        default: return "unknown";
    }
}

bool is_pe_name(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".exe" || ext == ".dll" || ext == ".sys";
}

int inspect_one(const std::filesystem::path& path, bool verbose) {
    const rt::pe::PeFile pe = rt::pe::load_pe_file(path.string());
    if (!pe.ok) {
        std::fprintf(stderr, "error: %s: %s\n", path.string().c_str(), pe.error.c_str());
        return 1;
    }

    std::printf("file: %s\n", path.string().c_str());
    std::printf("machine: 0x%x (%s)\n", pe.machine, machine_name(pe.machine));
    std::printf("sections: %u\n", pe.sections_count);
    std::printf("optional: %s\n", pe.pe32plus ? "PE32+" : "PE32");
    std::printf("entry_rva: 0x%x\n", pe.entry_rva);
    std::printf("image_base: 0x%llx\n", static_cast<unsigned long long>(pe.image_base));
    std::printf("size_of_image: 0x%x\n", pe.size_of_image);
    std::printf("subsystem: %u\n", pe.subsystem);
    std::printf("dll_characteristics: 0x%x\n", pe.dll_characteristics);
    std::printf("data_directories: %u\n", pe.num_data_dirs);

    if (verbose) {
        for (size_t i = 0; i < pe.sections.size(); ++i) {
            const auto& s = pe.sections[i];
            std::printf("section[%zu]: %-8s va=0x%x vsz=0x%x raw=0x%x rsz=0x%x\n", i, s.name.c_str(), s.virtual_address,
                        s.virtual_size, s.raw_ptr, s.raw_size);
        }
    }

    std::unordered_map<std::string, size_t> per_dll;
    for (const auto& imp : pe.imports) per_dll[imp.dll]++;
    std::printf("imports: dlls=%zu entries=%zu\n", per_dll.size(), pe.imports.size());
    if (verbose) {
        for (const auto& kv : per_dll) std::printf("  - %s (%zu)\n", kv.first.c_str(), kv.second);
    }

    if (pe.has_exports) {
        std::printf("exports: name=%s functions=%u names=%u\n", pe.exports.name.empty() ? "<none>" : pe.exports.name.c_str(),
                    pe.exports.functions, pe.exports.names);
    } else {
        std::printf("exports: none\n");
    }

    if (pe.relocs.present) {
        std::printf("relocs: blocks=%u entries=%u\n", pe.relocs.blocks, pe.relocs.entries);
    } else {
        std::printf("relocs: none\n");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bool verbose = false;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-v" || a == "--verbose") verbose = true;
        else args.push_back(std::move(a));
    }
    if (args.size() != 1) {
        std::fprintf(stderr, "usage: gta6rt_pe_info [-v] <pe-file-or-directory>\n");
        return 2;
    }

    const std::filesystem::path in(args[0]);
    std::error_code ec;
    if (std::filesystem::is_directory(in, ec)) {
        std::vector<std::filesystem::path> files;
        for (const auto& e : std::filesystem::directory_iterator(in, ec)) {
            if (ec) break;
            if (e.is_regular_file(ec) && is_pe_name(e.path())) files.push_back(e.path());
        }
        std::sort(files.begin(), files.end());
        if (files.empty()) {
            std::fprintf(stderr, "no .exe/.dll/.sys files found in %s\n", in.string().c_str());
            return 1;
        }
        int rc = 0;
        for (size_t i = 0; i < files.size(); ++i) {
            if (i) std::printf("\n");
            rc |= inspect_one(files[i], verbose);
        }
        return rc;
    }

    if (std::filesystem::is_regular_file(in, ec)) return inspect_one(in, true);
    std::fprintf(stderr, "error: not a file or directory: %s\n", in.string().c_str());
    return 1;
}
