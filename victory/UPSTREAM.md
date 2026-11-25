# Where this tree comes from

`victory/` is [Belobug/VIctory-Belobug-AMD](https://github.com/Belobug/VIctory-Belobug-AMD)
at `71652a2` (2026-11-25; upstream VIctory `main` up to `8e79c80` plus the AMD
fork's ~500 commits: lit world, vehicles, distant trees, hoisted clamps, ...),
with these on top:

| Change | From |
|--------|------|
| `VICTORY_NO_WATCHDOG` title watchdog hooks (`victory-loader/src/hooks.rs`) | Scarlight, via the local Belobug WIP |
| Copy-back reads into write-watched pages Wine refuses to write | Scarlight `21fe61f` |
| Root signature retried without heap-directly-indexed flags (vkd3d-proton) | Scarlight `bde296a` |
| No blending on integer render targets (vkd3d-proton refused those PSOs) | Scarlight `ceaf806` |
| SEH-guarded PSO creation (`seh.c`), subprocess PSO probe (`VICTORY_PROBE_PIPELINES=1`, hidden `_pipeline-helper`) | GTA6-btw's scarlight work |
| `VICTORY_LOG_FILE`, `VICTORY_SKIP_INT3`/`LEONIDA_SKIP_INT3`, 1.0.2.1 int3 NOP at `0x5149aec` | GTA6-btw's scarlight work |

Not carried over: Scarlight `ffa1310` (indirect clamp built without the
system HLSL compiler). Belobug compiles its clamp, compose and ambient shaders
with `D3DCompile`, so under Wine the prefix needs a native `d3dcompiler_47.dll`
(see `scripts/run_victory_proton.sh`).

The older Scarlight-based tree is still in `../scarlight/`.
