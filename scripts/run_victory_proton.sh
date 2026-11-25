#!/usr/bin/env bash
set -euo pipefail

# Run victory/ (Belobug's AMD fork + Wine fixes + GTA6-btw patches) under a
# Proton compat tool. Every setting can be overridden from the environment.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GTA6_DIR="${GTA6_DIR:-/home/joki/Downloads/GTA6.Xbox.v1.0.2.1}"
GAME_EXE="${GAME_EXE:-$GTA6_DIR/game_scarlett_master.exe}"
PROTON="${PROTON:-/home/joki/.local/share/Steam/compatibilitytools.d/Proton-CachyOS-Victory/proton}"
COMPAT_DATA="${STEAM_COMPAT_DATA_PATH:-/home/joki/.local/share/victory-proton}"
STEAM_CLIENT="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-/home/joki/.local/share/Steam}"
TARGET_DIR="$ROOT/victory/target/x86_64-pc-windows-msvc"
VICTORY_EXE="${VICTORY_EXE:-}"
if [[ -z "$VICTORY_EXE" ]]; then
  for candidate in "$TARGET_DIR/diag/victory.exe" "$TARGET_DIR/release/victory.exe" "$TARGET_DIR/debug/victory.exe"; do
    if [[ -f "$candidate" ]]; then VICTORY_EXE="$candidate"; break; fi
  done
fi
LOG_DIR="${LOG_DIR:-/tmp/opencode}"
TRACE_LOG="${VICTORY_LOG_FILE:-Z:\\tmp\\opencode\\victory-trace.log}"

mkdir -p "$LOG_DIR"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_CLIENT"
export STEAM_COMPAT_DATA_PATH="$COMPAT_DATA"
export WINEDEBUG="${WINEDEBUG:--all}"
export VICTORY_LOG="${VICTORY_LOG:-info}"
export VICTORY_LOG_FILE="$TRACE_LOG"
export MESA_VK_DEVICE_SELECT="${MESA_VK_DEVICE_SELECT:-1002:747e}"
export DRI_PRIME="${DRI_PRIME:-1}"

# Belobug's run settings (README "How it is run here"). Not its
# VICTORY_PRESENT_TARGET=1x1:1: that debug switch presents a 1x1 texture in
# place of the title's image (a black screen at 60 fps).
export VICTORY_HAIR="${VICTORY_HAIR:-1}"
# Scarlight's Linux settings (AMD-LINUX.md): the title's 30 s watchdog kills
# the process during long first-run pipeline builds; vkd3d-proton fences.
export VICTORY_NO_WATCHDOG="${VICTORY_NO_WATCHDOG:-1}"
export VKD3D_CONFIG="${VKD3D_CONFIG:-ignore_shared_fence}"
# GTA6-btw: skip the title's soft int3 asserts (diagnostic aid, not a fix).
export VICTORY_SKIP_INT3="${VICTORY_SKIP_INT3:-1}"
# Opt in to probing each new pipeline in a child process first
# (VICTORY_PROBE_PIPELINES=1); slow on the first run, so off by default.

if [[ ! -x "$PROTON" ]]; then
  echo "Proton not found/executable: $PROTON" >&2
  exit 1
fi
if [[ -z "$VICTORY_EXE" || ! -f "$VICTORY_EXE" ]]; then
  echo "victory.exe not found under $TARGET_DIR" >&2
  echo "Build: cd victory && cargo build --profile diag -p victory --features diagnostics --target x86_64-pc-windows-msvc" >&2
  exit 1
fi

echo "victory.exe: $VICTORY_EXE"
echo "trace log:   $TRACE_LOG"
exec "$PROTON" run "$VICTORY_EXE" run --execute --console series-x "$GAME_EXE" "$@"
