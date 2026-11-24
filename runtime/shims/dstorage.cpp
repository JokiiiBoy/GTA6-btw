// Minimal DirectStorage stub (XCRDAPI.dll / dstorage.dll). Lets boot proceed past
// DStorageGetFactory; DirectStorage requests are not actually serviced.
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <windows.h>
#include <initguid.h>
#include "shims.h"

namespace {

using Fn = HRESULT(STDMETHODCALLTYPE*)(void*, void*, void*, void*);
struct Object { Fn* vtbl; std::atomic<ULONG> refs{1}; };

static HRESULT STDMETHODCALLTYPE Stub(void*, void*, void*, void*) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE QI(void* self, REFIID, void** out) {
    if (!out) return E_POINTER;
    *out = self;
    static_cast<Object*>(self)->refs.fetch_add(1);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE AddRefFn(void* self) { return static_cast<Object*>(self)->refs.fetch_add(1) + 1; }
static HRESULT STDMETHODCALLTYPE ReleaseFn(void* self) {
    auto* o = static_cast<Object*>(self);
    ULONG r = o->refs.fetch_sub(1) - 1;
    if (r == 0) delete o;
    return r;
}

static Fn* make_vtbl() {
    constexpr size_t N = 128;
    auto* v = new Fn[N];
    for (size_t i = 0; i < N; ++i) v[i] = &Stub;
    v[0] = reinterpret_cast<Fn>(&QI);
    v[1] = reinterpret_cast<Fn>(&AddRefFn);
    v[2] = reinterpret_cast<Fn>(&ReleaseFn);
    return v;
}
static Fn* vtbl() { static Fn* v = make_vtbl(); return v; }

HRESULT STDMETHODCALLTYPE DStorageGetFactory(REFIID, void** out) {
    if (!out) return E_POINTER;
    static std::atomic<int> logged{0};
    if (logged.fetch_add(1) < 8) std::fprintf(stderr, "[rt] dstorage: DStorageGetFactory stub\n");
    *out = new Object{vtbl()};
    return S_OK;
}

}  // namespace

SHIM_TABLE(dstorage,
    {"xcrdapi.dll", "DStorageGetFactory", reinterpret_cast<void*>(&DStorageGetFactory)},
    {"dstorage.dll", "DStorageGetFactory", reinterpret_cast<void*>(&DStorageGetFactory)},
);