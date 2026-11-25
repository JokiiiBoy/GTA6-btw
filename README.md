# GTA6-btw

GTA6-btw is an experimental Linux/Wine boot-tracing workspace based on [Leonida](https://github.com/ItsNotRexed/Leonida), focused on getting the GTA6 Xbox build running further through its startup/runtime.

> **Status: experimental.** GTA6 does not boot fully yet.

## Current progress

- **Wine runtime path working:** cross-build `gta6rt.exe` with clang-cl/lld-link + xwin and run with `LEONIDA_SKIP_INT3=1 ./setup_leonida_wine.sh run`.
- **Startup tracing:** JSONL traces + summaries via `tools/boot_trace.py`; `file_open_failed` now includes capture stacks.
- **XMem fix:** fixed Wine/MSVC static-init issue in `placeholder_api()` so large Xbox `XMemVirtualAlloc` reservations succeed.
- **TLS fix:** general EXE TLS slot-0 redirect for modules with non-zero TLS index. This fixed hardcoded `gs:[0x58]` slot-0 accesses and moved boot significantly further.
- **GameInput fix:** added `GameInput.dll` ordinal `#1` alias for `GameInputCreate`; built with `LEONIDA_SDL3_FETCH=ON`.
- **DirectStorage stub:** added minimal `xcrdapi.dll!DStorageGetFactory` stub to get past unimplemented DirectStorage factory.
- **RAGE device mapping:** mapped `common:` and `platform:` device names to the game package root in the file system shim.
- **RPF/name search:** scanned the full 209GB dump with 6ixpack/sixng and gta6x (name recovery, structured recovery, `.zmt` scan, JOAAT hash checks) - no `version.txt` or `gameconfig.zmt` found by name/hash/extension/content.
- **victory-gta6 / victory-fork:** ported the `common:`/`platform:` mapping to both forks (`crates/victory-hle/src/modules/files.rs`); TLS/GameInput/DStorage/XErrorReport fixes were already present.
- **victory-fork on Linux (Proton-CachyOS):** cross-compiled `victory.exe` for `x86_64-pc-windows-msvc` on Linux using xwin/clang-cl/llvm-lib. Subprocess **probe** pipeline creation in `victory-gpu` (hidden `_pipeline-helper`): child probes PSO creation; on crash/fastfail it refuses, on success parent creates in-process. Avoids vkd3d-proton `__fastfail`/abort kills. Fixed a probe-build bug where missing pipeline library caused all recipe pipelines to be refused with `0xFFFFFFFF` (`drawn=0`). Needs retest. Wine-only run renders white/`drawn=0`.
- **Scarlight (Linux/AMD-focused victory fork):** copied to `scarlight/` (from `pablogonz12/Scarlight`). Ported SEH-guarded PSO creation + subprocess probe pipeline isolation (`VICTORY_PROBE_PIPELINES=1`, hidden `_pipeline-helper`) so vkd3d-proton fastfail/abort PSO crashes are refused instead of killing the process. Cross-checks pass for `victory-gpu` and `victory` on `x86_64-pc-windows-msvc`.
- **Current blocker:** gameconfig load fails (`platform:/data/gameconfig.zmt`, `common:/data/version.txt` not found), `int3` at `+0x2224517` skipped by `LEONIDA_SKIP_INT3`, bad config leads to crash at `game_scarlett_master.exe+0x5b29847` (`host:ucrtbase.dll+0x5fe3a`, memset write 0x0). Next options: supply correct files for this build, or add a temporary Leonida patch around config init to move boot forward.

## Quick start

You need Linux x86_64, Wine, `clang-cl`, `lld-link`, and Python 3. You must provide your own legally obtained GTA6 Xbox files.

```bash
./setup_leonida_wine.sh check
./setup_leonida_wine.sh install-xwin
./setup_leonida_wine.sh build-runtime

export GTA6_DIR="/path/to/GTA6.Xbox.v1.0.2.1"
LEONIDA_SKIP_INT3=1 ./setup_leonida_wine.sh run
```

Inspect traces:

```bash
./setup_leonida_wine.sh traces
python tools/boot_trace.py --latest ~/leonida-wine-logs
```

## Useful environment variables

| Variable | Purpose |
|---|---|
| `GTA6_DIR` | Path to the GTA6 Xbox package directory |
| `LEONIDA_SKIP_INT3=1` | Diagnostic: skip soft int3 asserts during boot tracing |
| `LEONIDA_WINEPREFIX` | Override Wine prefix (default: `~/.local/share/leonida-wine`) |
| `LEONIDA_XWIN_SDK` | Override xwin SDK path (default: `~/.local/share/xwin-sdk`) |

## Notes

- This repo does not include GTA6 game files, assets, keys, or extracted packages.
- Traces/logs can contain runtime details; review before sharing.
- `LEONIDA_SKIP_INT3` is a diagnostic aid, not a fix.

## Legal

For interoperability/research purposes only. Not affiliated with or endorsed by Rockstar Games, Take-Two, or Microsoft.