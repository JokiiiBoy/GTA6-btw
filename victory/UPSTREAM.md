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
system HLSL compiler) and `3f972c3` (Belobug already has a newer form).
Belobug compiles its clamp and ambient shaders with `D3DCompile`; Wine's
builtin `d3dcompiler_47` handles them (`argument_clamp_builds` and
`ambient_pass_builds` pass under the victory-proton prefix).

Known under Wine: `pipeline::library::tests::concurrent_atomic_writes_publish_one_complete_file`
fails (eight threads renaming over one file at once); the game's saves are
serialised by a lock, so it does not apply at run time.

The older Scarlight-based tree is still in `../scarlight/`.

## Wine: title memory the GPU uses in place

Belobug lets the host GPU use title memory in place (`OpenExistingHeapFromAddress1`).
Under Wine (vkd3d-proton on RADV) that is only correct for private memory: on a
view of a section (how xmem backs the title's physical pages there) the GPU
reads zeros and its writes never reach the title, so every frame was black at
60 fps and the title never left its first screen. `shadow.rs` now copies
section views under Wine (`VICTORY_DIRECT_MEMORY=1` opens them anyway).
`cargo run --example heap_probe` (victory-gpu, under the prefix) shows it:

    private VirtualAlloc: GPU read CPU data OK; CPU sees GPU write OK
    section SEC_COMMIT:   GPU read CPU data WRONG (0x00); CPU sees GPU write WRONG

Under Wine the title's 185 MB GPU heap (the one reservation `map_whole` shows
whole) is backed by private memory instead of a section view
(`xmem.rs`, `private_whole`; `VICTORY_WINE_SECTION_VIEWS=1` turns it off), so
the GPU uses it in place again: per-frame refreshes fell from 6-10 GB to about
2 GB. DirectStorage writes to its pages go to the private memory.

## Linux: host mappings are 4 KiB aligned

`shadow.rs` `sparse_writing` found a tile's start by rounding its CPU pointer
down to 64 KiB. Windows maps heaps 64 KiB aligned; vkd3d-proton only 4 KiB, so
it read before the mapping (`ucrtbase` memcpy access violation about three
minutes in, during the world's first load). It now subtracts the offset.
