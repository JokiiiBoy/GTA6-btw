#!/usr/bin/env bash
set -euo pipefail

# Run Scarlight/victory under a Proton compat tool for GTA6 Xbox boot tracing.
# Defaults target the custom Proton-CachyOS-Victory compat tool.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GTA6_DIR="${GTA6_DIR:-/home/joki/Downloads/GTA6.Xbox.v1.0.2.1}"
GAME_EXE="${GAME_EXE:-$GTA6_DIR/game_scarlett_master.exe}"
PROTON="${PROTON:-/home/joki/.local/share/Steam/compatibilitytools.d/Proton-CachyOS-Victory/proton}"
COMPAT_DATA="${STEAM_COMPAT_DATA_PATH:-/home/joki/.local/share/victory-proton}"
STEAM_CLIENT="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-/home/joki/.local/share/Steam}"
VICTORY_EXE="${VICTORY_EXE:-}"
if [[ -z "$VICTORY_EXE" ]]; then
  for candidate in \
    "$ROOT/scarlight/target/x86_64-pc-windows-msvc/debug/victory.exe" \
    "/home/joki/code/Scarlight/target/x86_64-pc-windows-msvc/debug/victory.exe"; do
    if [[ -f "$candidate" ]]; then VICTORY_EXE="$candidate"; break; fi
  done
  VICTORY_EXE="${VICTORY_EXE:-$ROOT/scarlight/target/x86_64-pc-windows-msvc/debug/victory.exe}"
fi
LOG_DIR="${LOG_DIR:-/tmp/opencode}"
TRACE_LOG="${VICTORY_LOG_FILE:-Z:\\tmp\\opencode\\victory-trace.log}"

mkdir -p "$LOG_DIR"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_CLIENT"
export STEAM_COMPAT_DATA_PATH="$COMPAT_DATA"
export WINEDEBUG="${WINEDEBUG:--all}"
export VICTORY_LOG="${VICTORY_LOG:-info}"
export VICTORY_LOG_FILE="$TRACE_LOG"
export VICTORY_SKIP_INT3="${VICTORY_SKIP_INT3:-1}"
export MESA_VK_DEVICE_SELECT="${MESA_VK_DEVICE_SELECT:-1002:747e}"
export DRI_PRIME="${DRI_PRIME:-1}"

if [[ ! -x "$PROTON" ]]; then
  echo "Proton not found/executable: $PROTON" >&2
  exit 1
fi
if [[ ! -f "$VICTORY_EXE" ]]; then
  echo "victory.exe not found: $VICTORY_EXE" >&2
  echo "Build: cd scarlight && cargo build -p victory --target x86_64-pc-windows-msvc --features diagnostics" >&2
  exit 1
fi

exec "$PROTON" run "$VICTORY_EXE" run --execute --console series-x "$GAME_EXE" "$@"
