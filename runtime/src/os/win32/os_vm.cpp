#include "../os.h"

#ifdef _WIN32
#include <windows.h>

namespace rt::os {

namespace {
DWORD to_win_prot(VmProt prot) {
    const bool r = has_prot(prot, VmProt::Read);
    const bool w = has_prot(prot, VmProt::Write);
    const bool x = has_prot(prot, VmProt::Exec);
    if (x && w) return PAGE_EXECUTE_READWRITE;
    if (x && r) return PAGE_EXECUTE_READ;
    if (x) return PAGE_EXECUTE;
    if (w) return PAGE_READWRITE;
    if (r) return PAGE_READONLY;
    return PAGE_NOACCESS;
}

VmProt from_win_prot(DWORD prot) {
    prot &= 0xFFu;  // mask modifiers like PAGE_GUARD
    switch (prot) {
        case PAGE_EXECUTE_READWRITE: return VmProt::Read | VmProt::Write | VmProt::Exec;
        case PAGE_EXECUTE_READ: return VmProt::Read | VmProt::Exec;
        case PAGE_EXECUTE: return VmProt::Exec;
        case PAGE_EXECUTE_WRITECOPY: return VmProt::Read | VmProt::Write | VmProt::Exec;
        case PAGE_READWRITE: return VmProt::Read | VmProt::Write;
        case PAGE_READONLY: return VmProt::Read;
        case PAGE_WRITECOPY: return VmProt::Read | VmProt::Write;
        default: return VmProt::None;
    }
}
}  // namespace

void* vm_reserve(void* hint, size_t size, bool fixed) {
    if (size == 0) return nullptr;
    void* p = VirtualAlloc(hint, static_cast<SIZE_T>(size), MEM_RESERVE, PAGE_NOACCESS);
    if (!p) return nullptr;
    if (fixed && hint && p != hint) {
        VirtualFree(p, 0, MEM_RELEASE);
        return nullptr;
    }
    return p;
}

bool vm_commit(void* addr, size_t size, VmProt prot) {
    if (!addr || size == 0) return false;
    return VirtualAlloc(addr, static_cast<SIZE_T>(size), MEM_COMMIT, to_win_prot(prot)) != nullptr;
}

bool vm_decommit(void* addr, size_t size) {
    if (!addr || size == 0) return false;
    return VirtualFree(addr, static_cast<SIZE_T>(size), MEM_DECOMMIT) != 0;
}

bool vm_release(void* addr, size_t /*size*/) {
    if (!addr) return false;
    return VirtualFree(addr, 0, MEM_RELEASE) != 0;
}

void* vm_alloc(void* hint, size_t size) {
    return VirtualAlloc(hint, static_cast<SIZE_T>(size), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

bool vm_protect(void* addr, size_t size, VmProt prot, VmProt* old_prot) {
    DWORD old = 0;
    const bool ok = VirtualProtect(addr, static_cast<SIZE_T>(size), to_win_prot(prot), &old) != 0;
    if (ok && old_prot) *old_prot = from_win_prot(old);
    return ok;
}

bool vm_free(void* addr, size_t size) {
    return vm_release(addr, size);
}

size_t page_size() {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    return static_cast<size_t>(si.dwPageSize);
}

size_t allocation_granularity() {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    return static_cast<size_t>(si.dwAllocationGranularity);
}

}  // namespace rt::os
#endif  // _WIN32

