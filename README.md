# GTA6-btw

GTA6-btw is an experimental fork/workspace based on [Leonida](https://github.com/ItsNotRexed/Leonida) for boot-tracing and runtime research on the GTA6 Xbox package under Wine on Linux.

This project is **not** a playable GTA6 port. Retail boot does not complete yet.

## Status

Current focus: getting further through GTA6 Xbox boot/runtime initialization via Leonida's Windows runtime (`gta6rt.exe`) under Wine.

What currently works in this workspace:

- Wine setup/build/run helper: `setup_leonida_wine.sh`
- Windows cross-build of `gta6rt.exe` (clang-cl/lld-link + xwin SDK)
- Startup trace logging (`--trace` JSONL, summarized by `tools/boot_trace.py`)
- `LEONIDA_SKIP_INT3=1` diagnostic skip for soft int3 asserts
- XMem placeholder API static-init fix (unblocks large Xbox `XMemVirtualAlloc` reservations)
- General TLS slot-0 redirect for EXE modules with non-zero TLS index (fixes hardcoded `gs:[0x58]` slot 0 access; inspired by VIctory's approach, implemented independently)

Current blocker:

- Boot progresses much further after TLS redirect (53246 gs:[0x58] loads redirected, heap bootstrap global becomes non-null). Current crash is `0xc0000005` read `0x0` at `game_scarlett_master.exe+0x5925903` (`rax=0x80004001`, `rcx=0`) with `XErrorReport` not served by `xgameruntime.dll`. Needs further shim/runtime investigation.

## What this repo contains

- Leonida runtime/source code and docs
- GTA6 boot-tracing/runtime fixes and experiments
- `setup_leonida_wine.sh` for Wine/xwin/build/run workflow

## What this repo does NOT contain

- GTA6 game files, assets, binaries, or extracted packages
- Wine prefix, xwin SDK cache, build outputs, or trace logs

You must supply your own legally obtained GTA6 Xbox package.

## Requirements

- Linux x86_64
- Wine (`wine` on PATH)
- Clang/LLVM with `clang-cl` and `lld-link`
- Python 3
- Leonida dependencies as documented in `docs/README.md` and `docs/build.md`
- xwin-splatted Windows SDK/MSVC CRT (the setup script can install/manage this)

## Quick start (Wine path)

From the repo root:

```bash
./setup_leonida_wine.sh check
./setup_leonida_wine.sh install-xwin
./setup_leonida_wine.sh build-runtime
```

Set the game directory (contains `game_scarlett_master.exe`):

```bash
export GTA6_DIR="/path/to/GTA6.Xbox.v1.0.2.1"
./setup_leonida_wine.sh dry-run
```

Run with startup trace:

```bash
LEONIDA_SKIP_INT3=1 ./setup_leonida_wine.sh run
```

Logs/traces are written to:

```text
~/leonida-wine-logs/run-YYYYMMDD-HHMMSS.jsonl
~/leonida-wine-logs/run-YYYYMMDD-HHMMSS.d3d12.log
```

## Useful commands

```bash
./setup_leonida_wine.sh help
./setup_leonida_wine.sh traces
python tools/boot_trace.py --latest ~/leonida-wine-logs
```

## Local data paths

| Path | Purpose | Commit? |
|---|---|---|
| `./` | Source code/scripts/docs | Yes |
| `runtime/build/` | Build output | No |
| `~/.local/share/leonida-wine/` | Wine prefix used by setup script | No |
| `~/.local/share/xwin-sdk/` | xwin SDK/cache | No |
| `~/leonida-wine-logs/` | Trace/log output | No, by default |
| GTA6 game directory | Your local game files | No |

## Debugging notes

- Use `LEONIDA_SKIP_INT3=1` only as a diagnostic to move past soft asserts.
- Startup traces can be large and may contain game/runtime information; keep them local.
- Retail GTA6 boot is expected to hit missing/unimplemented Leonida runtime behavior. See `docs/boot.md` and `docs/status.md`.

## Reference projects

These projects were cloned locally for reference during research (`/home/joki/code/refs/`):

- [TuxBox](https://github.com/brcly/TuxBox) (Apache-2.0)
- [xcl](https://github.com/xcl-dev/xcl) (GPL-2.0-only; reference only, do not copy code into GPLv3 codebase)
- [VIctory](https://github.com/marvisak/VIctory) (GPL-3.0-or-later)

## Legal

This repository is for interoperability/research purposes. It does **not** include GTA6 game data. Do not commit or distribute copyrighted game files, keys, or extracted assets.

Leonida/GTA6-btw is not affiliated with or endorsed by Rockstar Games, Take-Two, or Microsoft.