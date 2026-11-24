# GTA6-btw

GTA6-btw is an experimental Linux/Wine boot-tracing workspace based on [Leonida](https://github.com/ItsNotRexed/Leonida), focused on getting the GTA6 Xbox build running further through its startup/runtime.

> **Status: experimental.** GTA6 does not boot fully yet.

## Current progress

- **Wine runtime path working:** cross-build `gta6rt.exe` with clang-cl/lld-link + xwin and run with `LEONIDA_SKIP_INT3=1 ./setup_leonida_wine.sh run`.
- **Startup tracing:** JSONL traces + summaries via `tools/boot_trace.py`.
- **XMem fix:** fixed Wine/MSVC static-init issue in `placeholder_api()` so large Xbox `XMemVirtualAlloc` reservations succeed.
- **TLS fix:** general EXE TLS slot-0 redirect for modules with non-zero TLS index. This fixed hardcoded `gs:[0x58]` slot-0 accesses and moved boot significantly further.
- **GameInput fix:** added `GameInput.dll` ordinal `#1` alias for `GameInputCreate`; built with `LEONIDA_SDL3_FETCH=ON`.
- **DirectStorage stub:** added minimal `xcrdapi.dll!DStorageGetFactory` stub to get past unimplemented DirectStorage factory.
- **RAGE device mapping:** mapped `common:` and `platform:` device names to the game package root in the file system shim.
- **Current blocker:** crash in `host:ucrtbase.dll+0x5fe3a` (write `0x0`) called from `game_scarlett_master.exe+0x5b29847` after config validation fails (`int3` at `+0x1d5cc69` skipped by `LEONIDA_SKIP_INT3`). The game tries to load `common:\data\version.txt` and `platform:\data\gameconfig.zmt` (now mapped, but files not present as loose files in this package dump), leaving config uninitialized.

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