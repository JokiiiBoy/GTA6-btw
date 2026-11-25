#!/usr/bin/env bash
set -euo pipefail

# Run a VIctory tree under a Proton compat tool. Every setting can be
# overridden from the environment.
#   VICTORY_TREE=victory-dexnom (default): DexNom Runtime + GTA6-btw's Wine fixes
#   VICTORY_TREE=victory: Belobug's AMD fork + GTA6-btw's Wine fixes

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GTA6_DIR="${GTA6_DIR:-/home/joki/Downloads/GTA6.Xbox.v1.0.2.1}"
GAME_EXE="${GAME_EXE:-$GTA6_DIR/game_scarlett_master.exe}"
PROTON="${PROTON:-/home/joki/.local/share/Steam/compatibilitytools.d/Proton-CachyOS-Victory/proton}"
COMPAT_DATA="${STEAM_COMPAT_DATA_PATH:-/home/joki/.local/share/victory-proton}"
STEAM_CLIENT="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-/home/joki/.local/share/Steam}"
VICTORY_TREE="${VICTORY_TREE:-victory-dexnom}"
TARGET_DIR="$ROOT/$VICTORY_TREE/target/x86_64-pc-windows-msvc"
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
# Shader/pipeline caches, kept between runs and out of the repository:
# vkd3d-proton's compiled pipelines (it writes beside the working directory
# otherwise) and Mesa's RADV shader cache (1 GB by default, which the world's
# pipelines outgrow, so they were compiled again).
# Each tree keeps its own caches: their shader translations and pipeline
# keys differ.
if [[ "$VICTORY_TREE" == "victory" ]]; then
  CACHE_DIR="${VICTORY_HOST_CACHE_DIR:-$HOME/.cache/victory}"
else
  CACHE_DIR="${VICTORY_HOST_CACHE_DIR:-$HOME/.cache/$VICTORY_TREE}"
  # VIctory's own cache (translations, early pipeline list); saves stay shared.
  export VICTORY_CACHE_DIR="${VICTORY_CACHE_DIR:-C:\\users\\steamuser\\AppData\\Local\\$VICTORY_TREE\\cache}"
fi
mkdir -p "$CACHE_DIR/vkd3d"
# (with a path set vkd3d-proton names the file after the exe; an older
# cache left in the repository by earlier runs is moved over under that name)
if [[ -f "$ROOT/vkd3d-proton.cache" && ! -f "$CACHE_DIR/vkd3d/vkd3d-proton.victory.exe.cache" ]]; then
  mv "$ROOT/vkd3d-proton.cache" "$CACHE_DIR/vkd3d/vkd3d-proton.victory.exe.cache"
fi
rm -f "$ROOT/vkd3d-proton.cache.write"
export VKD3D_SHADER_CACHE_PATH="${VKD3D_SHADER_CACHE_PATH:-Z:${CACHE_DIR//\//\\}\\vkd3d}"
export MESA_SHADER_CACHE_MAX_SIZE="${MESA_SHADER_CACHE_MAX_SIZE:-10G}"
# VIctory builds helper shaders with D3DCompile. Wine's d3dcompiler_47 fails on
# the indirect-argument clamp, so every indirect draw and dispatch is dropped
# (black world). Install Microsoft's in the prefix: winetricks d3dcompiler_47
D3DC="$COMPAT_DATA/pfx/drive_c/windows/system32/d3dcompiler_47.dll"
if [[ ! -f "$D3DC" ]] || grep -qa "Wine builtin DLL" "$D3DC"; then
  echo "warning: no native d3dcompiler_47.dll in the prefix; the 3D world will stay black" >&2
fi
# How long a frame waits for pipelines still being compiled before it draws
# without them. DexNom's default is 12 s per frame ("the picture before frame
# rate"); under vkd3d-proton/RADV three compute pipelines never finish, so
# every world frame spent the whole 12 s waiting (0.07 fps). Pipelines used
# before are compiled ahead from the early list, so a short wait loses little.
export VICTORY_PIPELINE_WAIT_MS="${VICTORY_PIPELINE_WAIT_MS:-100}"
# Those three (1.0.2.1, RADV via vkd3d-proton 2026-11): left out instead of
# waited for every frame. Diagnostics builds only.
export VICTORY_SKIP_PIPELINES="${VICTORY_SKIP_PIPELINES:-559d9f0184e15fe3,a1b20bc47e3c9597,e8c962358d862e3f}"
# GTA6-btw: skip the title's soft int3 asserts (diagnostic aid, not a fix).
export VICTORY_SKIP_INT3="${VICTORY_SKIP_INT3:-1}"
# Console profile: series-x (default) or series-s. Series S renders at
# 1280x720: far less video memory and fewer copies, which a 12 GB card needs
# (KLBF Lab's recommended setting).
VICTORY_CONSOLE="${VICTORY_CONSOLE:-series-x}"
# Opt in to probing each new pipeline in a child process first
# (VICTORY_PROBE_PIPELINES=1); slow on the first run, so off by default.

if [[ ! -x "$PROTON" ]]; then
  echo "Proton not found/executable: $PROTON" >&2
  exit 1
fi
if [[ -z "$VICTORY_EXE" || ! -f "$VICTORY_EXE" ]]; then
  echo "victory.exe not found under $TARGET_DIR" >&2
  echo "Build: cd $VICTORY_TREE && cargo build --profile diag -p victory --features diagnostics --bins --target x86_64-pc-windows-msvc" >&2
  exit 1
fi

echo "tree:        $VICTORY_TREE"
echo "victory.exe: $VICTORY_EXE"
echo "console:     $VICTORY_CONSOLE"
echo "trace log:   $TRACE_LOG"
exec "$PROTON" run "$VICTORY_EXE" run --execute --console "$VICTORY_CONSOLE" "$GAME_EXE" "$@"
