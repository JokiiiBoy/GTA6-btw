# Native runtime: progress log

The goal: GTA VI's Xbox build running on Linux at the title's 30 fps. The plan for a Linux-native runtime is in
[linux_native_runtime.md](linux_native_runtime.md); this file records what is done, what was measured and what comes
next, so that work can stop and go on later. Newest entries first.

## Where the time goes (2026-11-26)

Under Proton, `victory-7900xtx`, release build, Series X profile, RX 7700 XT: about 107 ms a frame in the world
(9.3 fps median), of it **~38 ms copying title memory to the card's copies and ~52 ms copying it back**. The title's
own work and the card are not what limits it.

The copies exist because the card cannot use the title's memory where it is: VIctory maps the title's memory as views
of sections (shared memory), Wine backs those with a file, and Mesa's RADV imports host memory through libdrm's
`amdgpu_create_bo_from_user_mem`, which always passes `AMDGPU_GEM_USERPTR_ANONONLY`. The kernel then refuses memory
backed by a file (`amdgpu_ttm.c`: `if (gtt->userflags & AMDGPU_GEM_USERPTR_ANONONLY && vma->vm_file) return -EPERM`),
vkd3d-proton's `OpenExistingHeapFromAddress` gets nothing usable, and VIctory copies instead. A native runtime would
need the same: the title's memory shared between its views and the card.

## Steps

| # | Step | State |
|---|---|---|
| 1 | Shared title memory for the card: a libdrm without `ANONONLY` for the game only, and VIctory using the title's memory in place under Wine | in progress |
| 2 | Native runtime groundwork: the platform layer (`runtime/src/os/`), CMake for a Linux runtime (plan phase 1) | open |
| 3 | Linux PE loader: map the image, imports to shims, TEB/`gs:` and TLS, the Windows x64 calling convention at the import boundary (plan phase 2) | open |
| 4 | Execution: threads, synchronization, SEH and unwinding on signals, the C runtime the bundled DLLs need (plan phase 3) | open |
| 5 | Graphics: vkd3d-proton as a native library (or Leonida's Vulkan backend), presentation, input, audio (plan phase 4) | open |
| 6 | Game boot, then frame time (plan phase 5) | open |

## Log

### 2026-11-26 (late): no freeze any more; placed views alone make the frame slower

- The freeze was `place_in_section` opening the whole page-pool section as one heap the first time, under
  `shadow`'s `LOOSE` write lock: importing it touches every page of the section (gigabytes, swapping with 32 GB) while
  every list thread waits for the lock (stack dump at 60 s: `victory-list-*` in `RwLock::read_contended` in
  `shadow::loose`). Now each window imports only its own 64 KiB pages, from VIctory's own unguarded view of the whole
  section (`beside`), with `OpenExistingHeapFromAddress1`.
- In game (diagnostics build, Series X, 300 s): the world loads and runs, 80 windows (376 MB) in place, about 1,850
  turned down (not one run of a section), **4.2 fps with 1.7 GB of copies a frame** (without in place: ~9 fps release /
  6-7 diagnostics, ~0.1 GB). The same as `VICTORY_INPLACE=1` alone: memory in place must not have its write watch reset
  (under Wine that write-protects the pages and the driver waits and pins again, `inplace::pinned`), and every other
  copy that shares a 64 KiB block with it then loses its write tracking and is copied whole each frame.
- So in place pays only when almost all of the title's GPU memory is in place, or when the copies beside it are
  tracked without resets.

### 2026-11-26 (night): the card reads and writes the title's views in place; the game then freezes while loading

- With proton-btw's vkd3d-proton patch and the private libdrm (`LD_LIBRARY_PATH=~/.local/lib/victory-libdrm/lib`),
  VIctory's probe (`VICTORY_INPLACE_PROBE=1`) says for the first time: "the page pool's section in place: reads true,
  writes true, whatever the views do true". Natively, `hostimport.c` agrees.
- New in `victory-7900xtx` (`inplace.rs`): `VICTORY_INPLACE_VIEWS=1` (with `VICTORY_INPLACE=1`) places title memory that
  is one run of a view of a committed section (the page pool is committed under Wine already, `xmem.rs`) in a heap of
  the whole section (`place_in_section`, heaps kept per section in `SECTION_HEAPS`). In game: 47-61 windows
  (212-280 MB) in place, none refused; 189-218 still copied (not one run: scattered pages or holes).
- But the game then stops presenting while it loads the world (frame 418, no frame line for 5 minutes, no error).
  The likely cause, as `inplace::pinned` describes for private memory: `watch.rs` guards view pages by protecting
  them; every protection change of memory the card holds makes the kernel tell the driver, which waits for the card
  and pins again. Private memory in place is kept out of resets (`pinned`), view pages are not yet.
- `VICTORY_SKIP_INTRO=<s>` (Enter held every 2 s) gets runs past the start screen without anyone at the keys.

### 2026-11-26: two blockers found for the card using the title's memory under Wine

- **libdrm's `ANONONLY`** (see above): a private libdrm 2.4.134 without it is in `~/.local/lib/victory-libdrm`
  (checksum matched Arch's). A native test (`hostimport.c`: Vulkan `VK_EXT_external_memory_host` on RADV) shows it:
  private memory imports either way; a `MAP_SHARED` view of a memfd (as Wine maps a section) and anonymous
  `MAP_SHARED` memory are refused with the system libdrm and import with the private one. It loads in the game's
  processes with `LD_LIBRARY_PATH` (Proton keeps it behind its own folders and ships no libdrm).
- **vkd3d-proton's `OpenExistingHeapFromFileMapping`** maps a view of the section, imports it, and unmaps the view at
  once. On Windows the heap keeps the section; imported by address on Linux, the kernel drops the card's pages of the
  unmapped range, so the heap shows nothing of the section (VIctory's probe: "the GPU reads the view's bytes false"
  even with the private libdrm, and vkd3d-proton reports no failed import). Patched in proton-btw
  (`patches/vkd3d-proton/0001`): the view stays mapped while the heap lives.
- VIctory's `VICTORY_INPLACE=1` alone (private memory in place, views still copied) is slower here: 4 fps with 1.9 GB of
  copies a frame. It pays only with the page pool's views in place as well.

### 2026-11-26: step 1 started

- Confirmed in the sources: libdrm `amdgpu/amdgpu_bo.c` sets `AMDGPU_GEM_USERPTR_ANONONLY` in
  `amdgpu_create_bo_from_user_mem`; Linux `drivers/gpu/drm/amd/amdgpu/amdgpu_ttm.c` refuses `vm_file` memory with it,
  and allows it without.
- Measured: release build 9.3 fps against 6-7 for the diagnostics build; the system swaps (2.5 GB out in 3 minutes)
  with 32 GB of RAM.

## Next

Step 1, continued, in `victory-7900xtx` (run switches as in the log: `LD_LIBRARY_PATH=~/.local/lib/victory-libdrm/lib
VICTORY_INPLACE=1 VICTORY_INPLACE_VIEWS=1 VICTORY_SKIP_INTRO=40 VICTORY_BUILD=diag VICTORY_CONSOLE=series-x`):

1. Find which copies become "copied whole" with in place on (copystat's per-range lines: `copied from ... MB/frame`),
   and whether they share 64 KiB blocks with placed memory.
2. Track those copies without write-watch resets: compare their bytes (the card-side hashes `VICTORY_CARD_*` already do
   that for copies in the card's memory) or a no-reset kernel query, so a pinned neighbour does not force whole copies.
3. Place the scattered windows too: a reserved (tiled) buffer whose 64 KiB tiles are mapped to the section's pages
   (`UpdateTileMappings`), so that nearly all title GPU memory is in place.
4. Measure against ~9 fps / ~0.1 GB copies (release, Series X); only then put the switches into the run script.
