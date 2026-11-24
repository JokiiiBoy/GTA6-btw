#include "../os.h"

#if defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>

namespace rt::os {

namespace {
int to_posix_prot(VmProt prot) {
    int p = PROT_NONE;
    if (has_prot(prot, VmProt::Read)) p |= PROT_READ;
    if (has_prot(prot, VmProt::Write)) p |= PROT_WRITE;
    if (has_prot(prot, VmProt::Exec)) p |= PROT_EXEC;
    return p;
}
}  // namespace

void* vm_reserve(void* hint, size_t size, bool fixed) {
    if (size == 0) return nullptr;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_FIXED_NOREPLACE
    if (fixed && hint) flags |= MAP_FIXED_NOREPLACE;
#else
    if (fixed && hint) flags |= MAP_FIXED;
#endif
    void* p = mmap(hint, size, PROT_NONE, flags, -1, 0);
    if (p == MAP_FAILED) return nullptr;
    if (fixed && hint && p != hint) {
        munmap(p, size);
        return nullptr;
    }
    return p;
}

bool vm_commit(void* addr, size_t size, VmProt prot) {
    if (!addr || size == 0) return false;
    return mprotect(addr, size, to_posix_prot(prot)) == 0;
}

bool vm_decommit(void* addr, size_t size) {
    if (!addr || size == 0) return false;
    // Replace with a fresh PROT_NONE anonymous mapping to discard pages while keeping the range reserved.
    void* p = mmap(addr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    return p != MAP_FAILED;
}

bool vm_release(void* addr, size_t size) {
    if (!addr || size == 0) return false;
    return munmap(addr, size) == 0;
}

void* vm_alloc(void* hint, size_t size) {
    void* p = mmap(hint, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
}

bool vm_protect(void* addr, size_t size, VmProt prot, VmProt* old_prot) {
    if (old_prot) *old_prot = VmProt::Read | VmProt::Write;  // best-effort: Linux mprotect has no old-prot query
    return mprotect(addr, size, to_posix_prot(prot)) == 0;
}

bool vm_free(void* addr, size_t size) {
    return vm_release(addr, size);
}

size_t page_size() {
    const long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? static_cast<size_t>(ps) : 4096u;
}

size_t allocation_granularity() {
    return page_size();
}

}  // namespace rt::os
#endif  // __linux__

