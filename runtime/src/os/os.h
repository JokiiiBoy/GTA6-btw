#pragma once

#include <cstddef>
#include <cstdint>

namespace rt::os {

// Virtual memory protection flags. Portable subset needed by the PE loader.
enum class VmProt : uint32_t {
    None = 0,
    Read = 1,
    Write = 2,
    Exec = 4,
};

// Reserve address space only (no commit). Returns nullptr on failure.
// On Windows: MEM_RESERVE, PAGE_NOACCESS. On Linux: mmap PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS.
// If fixed is true, the mapping is placed exactly at hint (or fails) when the platform supports it.
void* vm_reserve(void* hint, size_t size, bool fixed = false);

// Commit pages inside a reserved region. Returns true on success.
// On Windows: MEM_COMMIT with the requested protection. On Linux: mprotect to the requested protection
// (anonymous pages are committed on fault; this also sets protection).
bool vm_commit(void* addr, size_t size, VmProt prot);

// Decommit pages, keeping the address range reserved. Returns true on success.
// On Windows: MEM_DECOMMIT. On Linux: mmap MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS PROT_NONE over the range
// (also discards pages) as a best-effort decommit.
bool vm_decommit(void* addr, size_t size);

// Release an address range obtained from vm_reserve/vm_alloc. Returns true on success.
// On Windows: MEM_RELEASE (size ignored by OS; pass the original size for Linux munmap).
bool vm_release(void* addr, size_t size);

// Reserve+commit convenience (backward compatible with Phase 1 groundwork).
// Returns RW memory. Equivalent to vm_reserve + vm_commit(Read|Write) on platforms where that split exists.
void* vm_alloc(void* hint, size_t size);

// Change protection on [addr, addr+size). Returns true on success.
// If old_prot is non-null, receives the previous protection when available; on Linux this is best-effort
// and may report Read|Write for mappings created by vm_alloc.
bool vm_protect(void* addr, size_t size, VmProt prot, VmProt* old_prot = nullptr);

// Free memory returned by vm_alloc. Returns true on success.
bool vm_free(void* addr, size_t size);

size_t page_size();
size_t allocation_granularity();

inline VmProt operator|(VmProt a, VmProt b) {
    return static_cast<VmProt>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool has_prot(VmProt v, VmProt bit) {
    return (static_cast<uint32_t>(v) & static_cast<uint32_t>(bit)) != 0;
}

}  // namespace rt::os

