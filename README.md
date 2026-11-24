# GTA6-btw

GTA6-btw is an experimental Linux/Wine boot-tracing workspace based on [Leonida](https://github.com/ItsNotRexed/Leonida), focused on getting the GTA6 Xbox build running further through its startup/runtime.

> **Status: experimental.** GTA6 does not boot fully yet.

## Current progress

- **Wine runtime path working:** cross-build `gta6rt.exe` with clang-cl/lld-link + xwin and run with `LEONIDA_SKIP_INT3=1 ./setup_leonida_wine.sh run`.
- **Startup tracing:** JSONL traces + summaries via `tools/boot_trace.py`.
- **XMem fix:** fixed Wine/MSVC static-init issue in `placeholder_api()` so large Xbox `XMemVirtualAlloc` reservations succeed.
- **TLS fix:** general EXE TLS slot-0 redirect for modules with non-zero TLS index. This fixed hardcoded `gs:[0x58]` slot-0 accesses and moved boot significantly further (53246 loads redirected in `game_scarlett_master.exe`).
- **Current blocker:** crash at `game_scarlett_master.exe+0x5925903` (read null, `rax=0x80004001`, `rcx=0`). `xgameruntime.dll!XErrorReport` is now exported; the remaining failure looks like an unimplemented/failed runtime API returning `E_NOTIMPL` and the game dereferencing a null result.

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