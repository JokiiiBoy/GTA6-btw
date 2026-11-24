// Portable OS VM abstraction smoke test (Phase 1 native Linux runtime groundwork).
// No game data, no GPU, no Windows-only APIs in the test itself.
#include "../src/os/os.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#define CHECK(condition) do {                                         \
    if (!(condition)) {                                               \
        std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); \
        return 1;                                                     \
    }                                                                 \
} while (0)

namespace {

int alloc_write_free() {
    const size_t sz = rt::os::page_size() * 2;
    void* p = rt::os::vm_alloc(nullptr, sz);
    CHECK(p != nullptr);
    std::memset(p, 0xA5, sz);
    auto* b = static_cast<unsigned char*>(p);
    CHECK(b[0] == 0xA5);
    CHECK(b[sz - 1] == 0xA5);
    CHECK(rt::os::vm_free(p, sz));
    return 0;
}

int protect_readonly() {
    const size_t sz = rt::os::page_size();
    void* p = rt::os::vm_alloc(nullptr, sz);
    CHECK(p != nullptr);
    std::memcpy(p, "abc", 4);
    CHECK(rt::os::vm_protect(p, sz, rt::os::VmProt::Read));
    CHECK(std::memcmp(p, "abc", 4) == 0);
    // Restore writable before free on implementations that require it.
    CHECK(rt::os::vm_protect(p, sz, rt::os::VmProt::Read | rt::os::VmProt::Write));
    CHECK(rt::os::vm_free(p, sz));
    return 0;
}

int reserve_commit_release() {
    const size_t sz = rt::os::page_size() * 4;
    void* p = rt::os::vm_reserve(nullptr, sz);
    CHECK(p != nullptr);
    CHECK(rt::os::vm_commit(p, rt::os::page_size() * 2, rt::os::VmProt::Read | rt::os::VmProt::Write));
    auto* b = static_cast<uint8_t*>(p);
    b[0] = 0x42;
    b[rt::os::page_size()] = 0x99;
    CHECK(b[0] == 0x42);
    CHECK(b[rt::os::page_size()] == 0x99);
    CHECK(rt::os::vm_decommit(p, rt::os::page_size() * 2));
    CHECK(rt::os::vm_commit(p, rt::os::page_size(), rt::os::VmProt::Read | rt::os::VmProt::Write));
    static_cast<uint8_t*>(p)[0] = 7;
    CHECK(static_cast<uint8_t*>(p)[0] == 7);
    CHECK(rt::os::vm_release(p, sz));
    return 0;
}

int reserve_fixed_hint() {
    const size_t sz = rt::os::page_size() * 2;
    void* p = rt::os::vm_reserve(nullptr, sz);
    CHECK(p != nullptr);
    CHECK(rt::os::vm_release(p, sz));
    // Best-effort: request same address fixed. Some platforms may refuse; that's acceptable.
    void* p2 = rt::os::vm_reserve(p, sz, true);
    if (p2) {
        CHECK(p2 == p);
        CHECK(rt::os::vm_commit(p2, sz, rt::os::VmProt::Read | rt::os::VmProt::Write));
        static_cast<uint8_t*>(p2)[0] = 1;
        CHECK(rt::os::vm_release(p2, sz));
    }
    return 0;
}

int page_size_sane() {
    const size_t ps = rt::os::page_size();
    CHECK(ps >= 4096);
    CHECK((ps & (ps - 1)) == 0);
    return 0;
}

}  // namespace

int main() {
    for (const auto test : {alloc_write_free, protect_readonly, reserve_commit_release, reserve_fixed_hint, page_size_sane})
        if (test()) return 1;
    std::printf("os_vm: alloc/write/free, protect, reserve/commit, page_size\n");
    return 0;
}
