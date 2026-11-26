#!/usr/bin/env bash
set -euo pipefail

# Run a VIctory tree under a Proton compat tool. Every setting can be
# overridden from the environment.
#   VICTORY_TREE=victory-dexnom (default): DexNom Runtime + GTA6-btw's Wine fixes
#   VICTORY_TREE=victory: Belobug's AMD fork + GTA6-btw's Wine fixes
#   VICTORY_TREE=victory-7900xtx: avatarmanz's RDNA 3 speed fork + GTA6-btw's
#     Wine fixes (victory-7900xtx/UPSTREAM.md), with that fork's settings

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
# VIctory-7900XTX's own settings (its scripts/play.ps1, without -Safe). Its
# VICTORY_SKIP_PIPELINES=none needs RUST_MIN_STACK (below): without it the three
# compute pipelines victory-dexnom skips never finish and the intro stutters
# waiting for them; with it they are made (tested 2026-11-26). VICTORY_BVH_BUILDER=1 is its -RayTracing (lit interiors,
# about a third slower). VICTORY_AMBIENT: 0 is the title's own ambient light (as
# victory-dexnom); the fork plays with 0.8, its estimate from the frame, which
# looked worse here.
# Added from zeex64's Linux settings (linux/local.env.example, measured on an RX 9070 XT under
# RADV): the pipeline book (every pipeline the title creates is written down, for `warm`),
# copies of 1 MiB and more in the card's memory (80 -> 60 ms a frame there), and two
# switches it measured as slower left off. Not its VICTORY_SKIP_BINK=1, which skips the
# logo and intro movies.
# VICTORY_FORK_SETTINGS=0: none of them (the settings victory-dexnom runs with).
if [[ "$VICTORY_TREE" == "victory-7900xtx" && "${VICTORY_FORK_SETTINGS:-1}" != "0" ]]; then
  for kv in VICTORY_TEXTURE_NO_WAIT=1 VICTORY_RELAY=2 VICTORY_WIDE_BACK=1 VICTORY_CARD_REHASH_RUNS=1 \
            VICTORY_JOIN_COPIES=1 VICTORY_INDIRECT_WINDOW=1 VICTORY_RELAY_COPY=1 VICTORY_CARD_STEPS=2 \
            VICTORY_INDIRECT_BATCH=2 VICTORY_MAP_THREADS=1 VICTORY_TITLE_PRIORITIES=0 VICTORY_CARD_AWAKE=1 \
            VICTORY_SHOW_AFTER=1 VICTORY_AMBIENT=0 VICTORY_D3D12_DRED=light VICTORY_SKIP_PIPELINES=none \
            VICTORY_THIN=1 VICTORY_JOIN_PARTS=8 VICTORY_UPLOAD_AHEAD=1 \
            VICTORY_PIPELINE_BOOK=1 VICTORY_CARD_BUFFERS_MIN_MB=1 VICTORY_GAPS_TOGETHER=0 \
            VICTORY_POOLS_MORE=0; do
    name="${kv%%=*}"
    if [[ -z "${!name+x}" ]]; then export "$kv"; fi
  done
fi
# Under vkd3d-proton a pipeline's DXIL becomes SPIR-V on the stack of the thread that asks
# for it, and the largest compute programs overflow Rust's default 2 MB thread stack; under
# Wine the thread dies silently and the pipeline stays "being made" for good (zeex64,
# linux/run.sh). The stack of every thread VIctory starts itself:
export RUST_MIN_STACK="${RUST_MIN_STACK:-67108864}"
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
# `run_victory_proton.sh warm [args]`: pipelines made ahead of a run (`victory warm`,
# default `--used`: those earlier runs drew with, from the pipeline book).
if [[ "${1:-}" == "warm" ]]; then
  shift
  [[ $# -gt 0 ]] || set -- --used
  exec "$PROTON" run "$VICTORY_EXE" warm "$@"
fi
# Before the title starts: the pipelines earlier runs used are made ahead
# (`victory warm --used`) while a "Compiling shaders" window is shown, so the
# intro does not wait for them. On by default for victory-7900xtx (its
# pipeline book lists them); VICTORY_WARM_FIRST=0 starts the title at once.
if [[ "${VICTORY_WARM_FIRST:-$([[ "$VICTORY_TREE" == "victory-7900xtx" ]] && echo 1 || echo 0)}" == "1" ]]; then
  echo "compiling shaders before the title starts..."
  "$PROTON" run "$VICTORY_EXE" warm --used > /dev/null 2>&1 &
  WARM=$!
  if command -v zenity > /dev/null && [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    ( while kill -0 "$WARM" 2> /dev/null; do echo "#Compiling shaders... ${SECONDS}s"; sleep 1; done; echo 100 ) |
      zenity --progress --pulsate --auto-close --no-cancel --title="GTA6-btw" \
        --text="Compiling shaders..." --width=320 2> /dev/null &
    DIALOG=$!
  fi
  wait "$WARM" || echo "warning: compiling shaders ahead failed; the title starts anyway" >&2
  [[ -n "${DIALOG:-}" ]] && wait "$DIALOG" 2> /dev/null
  echo "shaders ready"
fi
exec "$PROTON" run "$VICTORY_EXE" run --execute --console "$VICTORY_CONSOLE" "$GAME_EXE" "$@"
