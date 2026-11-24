# Native Linux runtime - audit and plan

Status: **proposal / Phase 0 audit**. This document maps the work needed to run
`gta6rt` as a native Linux host (no Wine/Proton) that loads the Xbox Series
Windows PE image, binds its imports to Leonida shims, and executes it on Linux
with the existing Vulkan D3D12 backend.

It is a plan, not a claim that the port is done. The README currently describes
the supported Linux path as: translator/assets/Vulkan tools natively; runtime
cross-compiled with clang-cl + xwin and run under Wine/Proton.

## Goal

Build a Linux-native `gta6rt` that can:

1. Map a Windows x64 PE executable and its bundled DLLs on Linux.
2. Bind imports to Linux implementations of Leonida's shim registry.
3. Execute game code with a compatibility layer for the Win32/Xbox/GDK ABI
   surface the title actually uses.
4. Present through Vulkan (reusing the D3D12_xs -> Vulkan work) and receive
   input/audio through Linux-native backends.

## Non-goals (initially)

- Full Wine replacement. Only the ABI surface Leonida needs is in scope.
- Running arbitrary Windows games.
- Perfect SEH/C++ exception parity on day one; start with loader/test fixtures,
  then grow coverage.
- Replacing the existing Wine/Proton path before the native path boots fixtures
  and reaches measurable parity.

## Current state

| Area | Current state | Linux portability |
|---|---|---|
| `make_game.py`, translator, asset tools | Python/std-lib or documented deps | Already native on Linux |
| Vulkan GPU test tools | Portable CMake targets | Already native on Linux |
| `runtime/src/vulkan_*` | Vulkan backend headers/helpers | Mostly portable, needs audit |
| `runtime/shims/d3d12_vulkan_device.cpp` | Vulkan-backed D3D12 device | Portable in intent, depends on Win32/COM shims |
| `runtime/src/pe_loader.*` | PE map/reloc/import/TLS/unwind | Win32-heavy |
| `runtime/src/main.cpp` | `wmain`, Win32 process/trace setup | Win32-only |
| `runtime/shims/*` | Win32/Xbox/GDK/D3D12 shims | Mostly Win32-only |
| CMake | `LEONIDA_BUILD_WINDOWS_RUNTIME` gates runtime on WIN32 | Needs platform split |

Evidence:

- `runtime/CMakeLists.txt`: `option(LEONIDA_BUILD_WINDOWS_RUNTIME ... ${WIN32})`;
  non-WIN32 only builds portable Vulkan/tool tests.
- `runtime/src/pe_loader.cpp`: `VirtualAlloc`, `VirtualProtect`,
  `RtlAddFunctionTable`, TLS slot management via `gta6rt_tls_host.dll`, SEH
  `__try/__except`, vectored handlers.
- `runtime/src/main.cpp`: `<windows.h>`, `wmain`, `GetModuleFileNameW`,
  process/thread startup assumptions.
- Almost every file under `runtime/shims/` and many under `runtime/src/`
  include `<windows.h>`.

## Dependency inventory

### Host/loader (critical path)

| File | Win32 dependencies | Linux direction |
|---|---|---|
| `runtime/src/main.cpp` | `wmain`, Win32 paths, console/process APIs | Make a platform-neutral CLI entry; keep Windows entry as wrapper |
| `runtime/src/pe_loader.cpp` | `VirtualAlloc/Protect`, `RtlAddFunctionTable`, TLS index/TEB, SEH, `SRWLOCK` | New `os/linux/*` backend: mmap/mprotect, libunwind/DWARF registration or custom unwind, pthread TLS strategy, sigaction/guard pages |
| `runtime/src/pe_loader.h` | Windows-leaning types/comments; `uint32_t` TLS index | Split platform-neutral image model from OS backing |
| `runtime/src/base_reservation.h` | `VirtualAllocEx`, process relaunch reservation | Linux `mmap` reservation / `MAP_FIXED_NOREPLACE` strategy |
| `runtime/src/code_hooks.h` | `VirtualProtect` | `mprotect` |
| `runtime/src/tls_host.cpp` | DLL TLS anchor | Replace with Linux TLS allocator/slot scheme; no DLL helper |
| `runtime/src/crt_timed_wait.cpp` | SRWLOCK/Win32 sync | pthread/ futex-backed equivalent |

### Shims (by portability)

| Subsystem | Files | Linux direction |
|---|---|---|
| D3D12/Xbox graphics | `shims/d3d12_*`, `shims/dxgi_*`, `src/vulkan_*` | Highest reuse. Keep D3D12_xs ABI, make COM/Win32 surface portable wrappers, drive Vulkan backend natively |
| File system | `shims/file_system.cpp` | Map GAME:/overlay semantics to POSIX paths; keep case/path rules explicit |
| Process/thread/library | `shims/process.cpp`, `shims/library_loader.cpp` | pthreads, dlopen for host libs where safe, PE-module lookup for bundled modules |
| Memory | `shims/xmem.cpp` | mmap/mprotect/madvise/placeholder equivalent; preserve Xbox allocation semantics used by title |
| Registry | `shims/registry.cpp` | Shim table is portable; unimplemented thunk machinery has Win32 unwind/assembly pieces |
| XGameRuntime/GDK | `shims/xgameruntime*` | Reimplement offline behavior against Linux-native state/files; no Xbox Live |
| GameInput | `shims/gameinput*` | SDL3 backend already exists; make build/runtime integration Linux-native |
| WinHTTP/offline | `shims/winhttp_offline.cpp` | Keep offline; remove WinHTTP dependency |
| Tracing/debug | `shims/startup_trace.h`, `shims/d3d12_trace.h` | Replace VEH/SEH/debug hooks with POSIX signals where needed |

### Build/test

| Area | Current | Linux direction |
|---|---|---|
| CMake | Windows runtime gated on WIN32 | Add `LEONIDA_BUILD_WINDOWS_RUNTIME` and future `LEONIDA_BUILD_LINUX_RUNTIME` or make runtime platform-selecting |
| Tests | Many Windows-only fixtures/exes; portable tests exist | Split portable tests from Windows PE fixtures; add Linux PE-fixture runner only where feasible |
| Scripts | `Build.bat`, PowerShell build scripts | Keep Windows scripts; add Linux equivalents or make Python entrypoints primary |

## Proposed architecture

### 1. Platform abstraction layer

Introduce a small OS interface used by loader/shims, not a giant framework:

```text
runtime/src/os/
  os.h              # allocation, protection, page size, time, threads, dylib
  win32/...
  linux/...
```

Required primitives:

- reserve/commit/free RW memory
- change protection
- query page size / allocation granularity
- high-resolution timer
- thread creation / TLS key or slot abstraction
- module/file path helpers
- debug/log/stack capture hooks

### 2. Split PE image model from OS backing

Keep PE parsing/binding logic platform-neutral where possible. Move OS-specific
parts out of `pe_loader.cpp`:

- image memory allocation -> `os::vm_*`
- section protection -> `os::vm_protect`
- unwind registration -> `os::unwind_register` (Windows: `RtlAddFunctionTable`;
  Linux: DWARF `.eh_frame` registration or libunwind-based strategy; exact choice
  needs prototype)
- TLS -> `os::tls_*` / loader-managed TLS blocks without Windows TEB slot hacks
- exception/SEH translation -> `os::exceptions_*`

### 3. Keep shim ABI stable, swap backends

The game imports Windows/Xbox DLL names. The shim registry should remain the
ABI boundary. Backends become:

- `registry` - portable dispatch, import classification, unimplemented thunks
- `shim_win32_*` - Windows-only host integration
- `shim_linux_*` - Linux-native behavior

D3D12_xs should keep its COM-facing ABI objects but avoid direct dependence on
Windows-only runtime services except behind `os/`.

### 4. Execution model decision

There are two realistic models:

| Model | Description | Pros | Cons |
|---|---|---|---|
| Native Linux PE loader | Linux ELF host maps PE and runs code directly | No Wine dependency; aligns with "native runtime" request | Must provide ABI/TLS/unwind/signal compatibility; high effort |
| Wine/Proton hybrid | Keep Windows host, improve Vulkan/native integration | Much lower effort; current project direction | Not a native Linux runtime |

This plan targets **Native Linux PE loader** because that was requested. The
hybrid remains the pragmatic fallback if native execution proves too costly.

## Phased milestones

### Phase 0 - Audit and design (this document)

Deliverables:

- [x] Dependency inventory for `runtime/src` and `runtime/shims`.
- [x] Architecture proposal and milestone plan.
- [ ] Confirm scope with maintainers (coordination issue / design discussion).
- [ ] Identify first Linux-runnable fixture target.

Acceptance:

- Document committed, linked from `docs/README.md`.

### Phase 1 - Build groundwork

Deliverables:

- Add CMake platform split: Windows runtime vs portable runtime core vs Linux
  runtime (experimental).
- Extract `os/` interfaces for VM/protect/time/path/thread primitives.
- Make portable runtime libraries compile with clang/gcc on Linux.
- Keep Windows behavior unchanged.

Acceptance:

- `cmake -S runtime -B runtime/build-linux` configures on Linux.
- Portable tests still pass; Windows runtime build unaffected.

### Phase 2 - Linux PE loader prototype

Deliverables:

- Linux VM backend for image mapping and section protection.
- Load synthetic PE fixtures (tiny_dll/tiny_exe style) on Linux.
- Resolve imports against shim registry/module graph.
- Implement minimal TLS strategy for single-threaded fixture execution.

Acceptance:

- A small PE fixture loads and reports imports on Linux.
- No game code required.

### Phase 3 - Minimal execution and shim parity

Deliverables:

- Linux implementations for file system, process, library loader, xmem basics.
- Unimplemented thunk/logging path works on Linux.
- Thread creation and basic synchronization for fixtures.
- Exception/unwind strategy prototype (signals + unwind metadata).

Acceptance:

- `--dry-run`/fixture init executes selected test images natively on Linux.

### Phase 4 - Graphics/input bring-up

Deliverables:

- D3D12_xs/Vulkan device path builds and runs on Linux.
- SDL3/GameInput Linux integration.
- Presentation path (swapchain/surface) on Linux.

Acceptance:

- Vulkan-backed D3D12 tests/fixtures run on Linux.

### Phase 5 - Game boot bring-up

Deliverables:

- Incremental GDK/Xbox shim coverage driven by missing imports and traces.
- Performance/profiling, crash diagnostics, compatibility matrix.
- Developer docs and CI.

Acceptance:

- Measurable boot progress on real package under native Linux runtime.

## Risks

- **ABI/exception semantics:** Windows SEH, C++ unwinding, TLS, and loader
  behavior are subtle. A Linux PE loader can pass fixtures and still fail on
  real game code.
- **Scope creep into Wine:** Avoid implementing a generic Windows emulator.
  Implement only what Leonida needs, guided by traces/tests.
- **Maintenance cost:** Every Windows runtime change may need Linux parity.
  Platform abstraction must stay small and tested.
- **Legal/project policy:** No game data/keys; all tests must be synthetic or
  user-local.

## First concrete tasks

1. **CMake audit/split**
   - Add Linux runtime configure path that compiles only portable units first.
   - Move Windows-only flags (`/W4`, `/utf-8`, `/EHsc`, `/OPT:NOREF`) behind
     MSVC/clang-cl checks.

2. **OS abstraction skeleton**
   - Create `runtime/src/os/os.h` and Win32 backend wrapping current calls.
   - No behavior change on Windows.

3. **PE loader seam**
   - Extract `VirtualAlloc/Protect` and unwind registration calls into OS
     functions.
   - Keep image parsing/binding unchanged.

4. **Linux fixture target**
   - Pick one tiny PE fixture test and define the Linux-native equivalent.

5. **Design discussion**
   - Post plan in coordination issue before large implementation PRs, per
     `AGENTS.md`.

## Validation commands (Linux, as groundwork lands)

```bash
cmake -S runtime -B runtime/build-linux
cmake --build runtime/build-linux -j
ctest --test-dir runtime/build-linux --output-on-failure
```

Initially this should run only portable tests; Linux runtime tests get added as
the PE loader/platform layer becomes viable.

## Notes

- This doc intentionally does not include game data, keys, or decrypted assets.
- Any new behavior claim should be backed by code, tests, or measured traces.
- The existing Wine/Proton path remains supported until native Linux reaches
  comparable confidence.
