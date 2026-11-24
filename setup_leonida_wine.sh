#!/usr/bin/env bash
set -euo pipefail

# Leonida GTA6 Wine setup/run helper.
# This sets up a Windows cross-build of Leonida's runtime and runs boot traces
# under Wine. It does NOT make GTA6 boot - Leonida's first retail --run has not
# been achieved yet (see Leonida docs/boot.md).

REPO="${LEONIDA_REPO:-/home/joki/code/Leonida}"
GAME_DIR="${LEONIDA_GAME_DIR:-/home/joki/Downloads/GTA6.Xbox.v1.0.2.1}"
XWIN_DIR="${XWIN_DIR:-$HOME/.local/share/xwin-sdk}"
WINEPREFIX="${WINEPREFIX:-$HOME/.local/share/leonida-wine}"
WIN_BUILD="${LEONIDA_WIN_BUILD:-$REPO/runtime/build/windows}"
SDK_VERSION="${XWIN_SDK_VERSION:-10.0.26100}"
LOG_DIR="${LEONIDA_LOG_DIR:-$HOME/leonida-wine-logs}"

usage() {
  cat <<EOF
Usage: $(basename "$0") <command> [args]

Commands:
  check                 Check prerequisites and current status
  install-xwin          Install xwin via cargo (requires Rust/cargo)
  splat-sdk             Download/splat Windows SDK+CRT with xwin (accepts MS license)
  build-runtime         Cross-build Windows gta6rt.exe with clang-cl + xwin
  setup                 check + splat-sdk(if needed) + build-runtime
  dry-run [exe]         Run gta6rt --dry-run under Wine
  init [exe]            Run gta6rt --init under Wine
  run [exe]             Run gta6rt --run under Wine with boot traces
  traces                Summarize latest boot traces with tools/boot_trace.py

Environment overrides:
  LEONIDA_REPO          Leonida source repo (default: $REPO)
  LEONIDA_GAME_DIR      GTA6 Xbox package dir (default: $GAME_DIR)
  XWIN_DIR              xwin splat dir (default: $XWIN_DIR)
  WINEPREFIX            Wine prefix (default: $WINEPREFIX)
  LEONIDA_WIN_BUILD     Windows runtime build dir (default: $WIN_BUILD)
  XWIN_SDK_VERSION      Windows SDK version for xwin (default: $SDK_VERSION)
  LEONIDA_LOG_DIR       Trace/log output dir (default: $LOG_DIR)
  LEONIDA_UNIMPLEMENTED log|fail|stop (default: log)
  LEONIDA_VK_VALIDATION 1 to enable Vulkan validation

Notes:
  - The target game executable must be the Xbox/Windows PE, usually:
      \$LEONIDA_GAME_DIR/game_scarlett_master.exe
  - Trace files are created fresh; existing trace paths are refused by gta6rt.
  - Keep traces/logs local; they may contain game output/data.
EOF
}

die() { echo "error: $*" >&2; exit 1; }
info() { echo "==> $*" >&2; }

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

have_cmd() { command -v "$1" >/dev/null 2>&1; }

wine_to_z() {
  local p
  p="$(realpath "$1")"
  printf 'Z:%s' "${p//\//\\}"
}

game_exe() {
  local exe="${1:-$GAME_DIR/game_scarlett_master.exe}"
  [[ -f "$exe" ]] || die "game executable not found: $exe"
  printf '%s\n' "$exe"
}

check_prereqs() {
  local ok=1
  for c in wine cmake ninja python3 git clang-cl lld-link llvm-lib llvm-rc llvm-mt; do
    if have_cmd "$c"; then
      printf 'ok   %s -> %s\n' "$c" "$(command -v "$c")"
    else
      printf 'MISS %s\n' "$c"
      ok=0
    fi
  done
  if have_cmd clang-cl; then
    clang-cl --version | head -1 || true
  fi
  if have_cmd xwin; then
    printf 'ok   xwin -> %s\n' "$(command -v xwin)"
    xwin --version 2>&1 | head -1 || true
  else
    printf 'MISS xwin\n'
    ok=0
  fi
  if [[ -d "$XWIN_DIR/VC/Tools/MSVC" ]] && compgen -G "$XWIN_DIR/Windows Kits/10/Lib/*/um/x64" >/dev/null; then
    printf 'ok   xwin SDK -> %s\n' "$XWIN_DIR"
  else
    printf 'MISS xwin SDK splat -> %s\n' "$XWIN_DIR"
    ok=0
  fi
  if [[ -x "$WIN_BUILD/gta6rt.exe" ]]; then
    printf 'ok   gta6rt.exe -> %s\n' "$WIN_BUILD/gta6rt.exe"
  else
    printf 'MISS gta6rt.exe -> %s\n' "$WIN_BUILD/gta6rt.exe"
    ok=0
  fi
  if [[ -f "$GAME_DIR/game_scarlett_master.exe" ]]; then
    printf 'ok   game exe -> %s\n' "$GAME_DIR/game_scarlett_master.exe"
  else
    printf 'MISS game exe -> %s\n' "$GAME_DIR/game_scarlett_master.exe"
    ok=0
  fi
  [[ $ok -eq 1 ]] || return 1
}

install_xwin() {
  need_cmd cargo
  info "Installing xwin with cargo..."
  cargo install xwin
}

splat_sdk() {
  need_cmd xwin
  mkdir -p "$(dirname "$XWIN_DIR")"
  info "Splatting Windows SDK/CRT with xwin into $XWIN_DIR"
  info "This downloads Microsoft components and requires accepting Microsoft's license."
  xwin --accept-license --sdk-version "$SDK_VERSION" splat \
    --use-winsysroot-style --preserve-ms-arch-notation \
    --output "$XWIN_DIR"
}

build_runtime() {
  need_cmd cmake
  need_cmd ninja
  need_cmd clang-cl
  need_cmd lld-link
  [[ -d "$REPO/runtime" ]] || die "Leonida repo not found: $REPO"
  [[ -d "$XWIN_DIR/VC/Tools/MSVC" ]] || die "missing xwin SDK; run: $0 splat-sdk"
  info "Configuring Windows cross-build..."
  cmake -S "$REPO/runtime" -B "$WIN_BUILD" -G Ninja \
    --toolchain "$REPO/runtime/cmake/clang-cl-xwin.cmake" \
    -DXWIN_DIR="$XWIN_DIR"
  info "Building Windows runtime..."
  cmake --build "$WIN_BUILD" --parallel
  [[ -f "$WIN_BUILD/gta6rt.exe" ]] || die "build finished but gta6rt.exe not found in $WIN_BUILD"
  info "Built $WIN_BUILD/gta6rt.exe"
}

ensure_runtime() {
  if [[ ! -f "$WIN_BUILD/gta6rt.exe" ]]; then
    info "Windows runtime missing; running setup build..."
    setup_all
  fi
}

setup_all() {
  check_prereqs || true
  if ! have_cmd xwin; then
    die "xwin not installed. Install it (cargo install xwin) or make it available, then rerun."
  fi
  if [[ ! -d "$XWIN_DIR/VC/Tools/MSVC" ]] || ! compgen -G "$XWIN_DIR/Windows Kits/10/Lib/*/um/x64" >/dev/null; then
    splat_sdk
  fi
  build_runtime
  info "Setup complete."
  info "Try: $0 dry-run"
}

run_mode() {
  local mode="$1"; shift
  local exe
  exe="$(game_exe "${1:-}")"
  [[ -f "$WIN_BUILD/gta6rt.exe" ]] || die "missing $WIN_BUILD/gta6rt.exe; run: $0 setup"
  need_cmd wine
  need_cmd python3
  mkdir -p "$LOG_DIR" "$WINEPREFIX"

  local ts base trace log
  ts="$(date +%Y%m%d-%H%M%S)"
  base="$LOG_DIR/${mode}-${ts}"
  trace="${base}.jsonl"
  log="${base}.d3d12.log"
  [[ ! -e "$trace" ]] || die "trace already exists: $trace"

  local exe_z game_z trace_z
  exe_z="$(wine_to_z "$exe")"
  game_z="$(wine_to_z "$GAME_DIR")"
  trace_z="$(wine_to_z "$trace")"

  info "Mode: $mode"
  info "Game: $exe"
  info "Trace: $trace"
  info "D3D12 log: $log"

  export WINEPREFIX
  export WINEDEBUG="${WINEDEBUG:--all}"
  export LEONIDA_D3D12="${LEONIDA_D3D12:-vulkan}"
  export LEONIDA_UNIMPLEMENTED="${LEONIDA_UNIMPLEMENTED:-log}"
  export LEONIDA_SKIP_INT3="${LEONIDA_SKIP_INT3:-0}"

  set +e
  if [[ "$mode" == "run" ]]; then
    wine "$WIN_BUILD/gta6rt.exe" \
      --run "$exe_z" \
      --game-dir "$game_z" \
      --trace "$trace_z" \
      --trace-d3d12 \
      2> "$log"
  else
    # dry-run/init: still capture stderr; --trace is supported for these modes too.
    wine "$WIN_BUILD/gta6rt.exe" \
      "--$mode" "$exe_z" \
      --game-dir "$game_z" \
      --trace "$trace_z" \
      --trace-d3d12 \
      2> "$log"
  fi
  local rc=$?
  set -e

  info "gta6rt exit code: $rc"
  if [[ -f "$trace" || -f "$log" ]]; then
    info "Trace summary:"
    python3 -I "$REPO/tools/boot_trace.py" "$trace" --d3d12 "$log" || true
  else
    info "No trace/log files were produced."
  fi
  return $rc
}

summarize_traces() {
  need_cmd python3
  local latest_trace latest_log
  latest_trace="$(ls -1t "$LOG_DIR"/*.jsonl 2>/dev/null | head -1 || true)"
  [[ -n "$latest_trace" ]] || die "no trace files in $LOG_DIR"
  latest_log="${latest_trace%.jsonl}.d3d12.log"
  info "Summarizing $latest_trace"
  python3 -I "$REPO/tools/boot_trace.py" "$latest_trace" --d3d12 "$latest_log"
}

cmd="${1:-help}"
shift || true
case "$cmd" in
  help|-h|--help) usage ;;
  check) check_prereqs ;;
  install-xwin) install_xwin ;;
  splat-sdk) splat_sdk ;;
  build-runtime) build_runtime ;;
  setup) setup_all ;;
  dry-run) run_mode dry-run "$@" ;;
  init) run_mode init "$@" ;;
  run) run_mode run "$@" ;;
  traces) summarize_traces ;;
  *) usage; exit 2 ;;
esac
