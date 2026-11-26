# Where this tree comes from

`victory-7900xtx/` is [avatarmanz/VIctory-7900XTX](https://github.com/avatarmanz/VIctory-7900XTX)
at `0a43566` (2026-11-25), a speed fork of Marvisak's VIctory for AMD RDNA 3, with GTA6-btw's
Wine/Linux pieces from `victory-dexnom/` merged on top.

Then merged with git: [zeex64/VIctory-7900XTX](https://github.com/zeex64/VIctory-7900XTX) at
`e2b05f4` (2026-11-26), a fork of the same commit about running under Proton on RADV (RX 9070 XT):
28 commits (frame-time work, copy queue, Bink skipping, crash reports, `linux/` scripts and Wine
patches). Two conflicts: `shader_cache.rs` (both setting names kept) and `windows.rs` (its check
of `VICTORY_NO_WATCHDOG`, which also takes `0` as off). Its Wine pieces overlapped ours without
duplicates. Build and `cargo test --release -p victory-shader --lib` (206 passed) checked.

From its `linux/run.sh` the run script takes `RUST_MIN_STACK=64 MiB`: without it the largest
compute pipelines overflow the thread stack inside vkd3d-proton and the thread dies silently under
Wine; that is why three compute pipelines "never finished" here. With it they are made, so nothing
is skipped any more. Measured 2026-11-26 on an RX 7700 XT (victory-ntdll Proton): world at
6.6-10.3 fps (victory-dexnom: 1-4), the intro at a steady 30 fps once the caches are warm.

Looked at and left out: upstream PR #3163 shows that the title falls back to its ordinary
descriptions when device methods 0x1c8/0x218 (its cache of precompiled native pipelines) fail, so
they keep failing; answering them (as 101Leafy's `VICTORY_LOOSE_PIPELINES` does for 0x218) lost
draws upstream. The draws still dropped are the ray-tracing structure builder's, which is off: it
does not run usably under RADV (zeex64), and `VICTORY_AMBIENT` is the stand-in for its light.

## What was merged from victory-dexnom

The difference between `victory-dexnom/` and DexNom/VIctory `5f4c3df` (the commit it was copied
from), 15 files, applied with a three-way merge:

- Wine hooks: `crates/victory-loader/src/hooks.rs` (`VICTORY_NO_WATCHDOG`), `windows.rs`, `lib.rs`.
- Logging to a file under Proton: `VICTORY_LOG_FILE` (`victory-core/src/logging.rs`), crash
  reports (`crash.rs`) and stack dumps (`stacks.rs`, `say`) written to the log as well.
- The 1.0.2.1 int3 NOP and `VICTORY_SKIP_INT3` (`prepare.rs`).
- The early pipeline list kept without a host pipeline library (`pipeline/library.rs`).
- KLBF Lab's half-wave port: `VICTORY_PS_HALVES`, `set_host_pixel_wave`, `VICTORY_HOST_WAVE`
  (`victory-shader/src/emit/lower.rs`, `emit/mod.rs`, `victory-gpu/src/host.rs`, `shader_cache.rs`).
- Wine copy-back into write-watched pages (`host.rs` `write_mapped`).
- `VICTORY_PIPELINE_DUMP=<dir>`: dumps what the title passes to device methods 0x1c8 and 0x218
  (`victory-gpu/src/device.rs`). Title data: keep that folder out of the repository.
- `.cargo/config.toml`: the Linux cross build for `x86_64-pc-windows-msvc`.

Conflicts and how they were resolved:

- `shader_cache.rs`: this fork keys translations by `VICTORY_TRANSLATOR` and `host_settings()`;
  the host pixel wave was added to that key, and `VICTORY_PS_HALVES` to its list of settings.
- `stacks.rs`: this fork's version (thread affinity, `name_of`), with `say` in place of `eprintln!`.
- `host.rs` wave setup: `VICTORY_HOST_WAVE` and `set_host_pixel_wave`, plus this fork's
  `set_host_rdna`.
- `host.rs` `write_mapped`: this fork's retry (`watch::writing`, `again_after_change`); the Wine
  read fallback runs before a copy is given up as lost.
- `emit/mod.rs`: both sets of exports.

Checked: the Windows cross build (`cargo build --profile diag -p victory --features diagnostics
--bins --target x86_64-pc-windows-msvc`) and `cargo test --release -p victory-shader --lib` (203
passed). Under Proton (victory-ntdll, RX 7700 XT): boots with the watchdog hooks and int3 NOP
applied, and the intro runs smoothly with the settings below.

Run it with `VICTORY_TREE=victory-7900xtx scripts/run_victory_proton.sh`; the script then sets this
fork's own settings (`scripts/play.ps1`) instead of victory-dexnom's.

Under Proton the run script differs from this fork's `play.ps1` in `VICTORY_AMBIENT=0` (the
title's own ambient light), and adds zeex64's measured `VICTORY_PIPELINE_BOOK=1`,
`VICTORY_CARD_BUFFERS_MIN_MB=1`, `VICTORY_GAPS_TOGETHER=0` and `VICTORY_POOLS_MORE=0`.
`scripts/run_victory_proton.sh warm` makes the pipelines of earlier runs ahead (`victory warm --used`). `VICTORY_FORK_SETTINGS=0` turns all of this fork's settings off.

`VICTORY_BUILDER_RUN=<hash>,...` (diagnostics, `pipeline.rs`, added here): compute programs taken
for structure builder stages (they read a spilled user-data table) that run as ordinary pipelines
all the same; each one taken is logged once. Found with it (2026-11-26): the 52 such programs in the
opening are the builder's (buffer stores only); with all of them running nothing is dropped, the
phone's screen shows (it needs the finished structures), and the world runs at 3.8-5.8 fps
instead of 6-10. Frozen mouths are not from this: they do not move in victory-dexnom either.

Startup (run script, 2026-11-26): before the title starts, `victory warm --used` makes the
pipelines earlier runs used (from the pipeline book) while a zenity window says "Compiling
shaders...", so the intro waits for none (seconds with warm caches; longer after a new build or
driver). `VICTORY_WARM_FIRST=0` starts the title at once.

"present times" (diagnostics, `host.rs`, added here): every 5 s, the median, 95th and 99th percentile and longest
time between presents, and the share over 50 ms; a 5-second mean frame rate hides a stutter, these show it.
