# Pokémon Emerald — RG Nano native port: handoff

Status as of **2026-08-28**. Branch `rg-nano-port`. This picks up from the
ChatGPT/Codex session that wrote the port; that session ran out of budget before
it ever ran the binary on hardware, so everything below the "What was already
here" line was found by actually booting it on the device.

**Where it stands: playable.** The game boots, runs at authentic 59.7275 Hz
timing, has working controls, sound, saves, and the companion panel switching
context with what is happening in-game. Battles work with the stock top-screen
UI plus a mirrored action/move grid on the lower panel.

Still open: the release (ROM-gated) build has never been produced or tested —
everything so far is `DEVELOPMENT_BUILD=1` — and one unreproduced crash on
entering a battle. See [Open issues](#open-issues).

---

## Quick reference

| Thing | Value |
|---|---|
| Device | RG Nano at `192.168.1.222`, root/`funkey`, key auth |
| Frontend actually running | **`retrofe`** (not gmenu2x, despite `../CLAUDE.md`) |
| OPK on device | `/mnt/Native games/PokemonEmeraldNano_funkey-s.opk` |
| Writable data dir | `/mnt/FunKey/.pokemon-emerald-nano/` (`pokeemerald.sav`, `last.log`, `prev.log`) |
| SDK | `~/funkey-sdk-2.3.0` inside WSL Ubuntu |
| Build | `make -f Makefile_rg_nano` |
| Package | `DEVELOPMENT_BUILD=1 scripts/package_rg_nano.sh` |
| Current build type | **development** (no asset manifest, no ROM needed at runtime) |

Build + deploy + capture a screenshot, all in one (scripts live in the scratchpad
of the session that made them; recreate as needed):

```bash
wsl.exe -d Ubuntu bash -c 'export PATH="$HOME/funkey-sdk-2.3.0/bin:$PATH"; cd /mnt/c/Programmering/SBC/RG_Nano/Pokemon_Emerald_RG_Nano && make -f Makefile_rg_nano && DEVELOPMENT_BUILD=1 bash scripts/package_rg_nano.sh'
```

### You cannot see the screen over SSH — but you can grab it

`/dev/fb0` is 240×720 RGB565 (three 240×240 pages). Dump the visible page and
convert it locally. **Capture while the app is still alive** — if you `dd` after
it exits you get the frontend's wallpaper repainted over the top, which is easy
to mistake for game output (I made exactly that mistake once).

```bash
dd if=/dev/fb0 of=/tmp/fb.raw bs=115200 count=1
```

Then convert RGB565 → PNG host-side (a ~30-line pure-Python zlib PNG writer is
enough; PIL is not installed in WSL).

### DANGER: never use `frontend set none` from a launcher

**`frontend set none` writes `/mnt/disable_frontend`, and `/mnt` is the
persistent vfat partition.** The launcher loop checks that file on every
iteration and refuses to start, so the setting survives reboots. Any launch that
does not reach its matching `frontend set <name>` — a SIGKILL, a crash at the
wrong moment, a flat battery — leaves the console booting with **no launcher and
no working buttons**, recoverable only over SSH.

I did exactly this: a test harness ended each run with `kill -9` on `run.sh`,
SIGKILL cannot be trapped, the restore never ran, and the device was left
unusable. `packaging/rg-nano/run.sh` therefore does **not** touch the frontend
at all, and `scripts/`-style helpers must never SIGKILL it either — send SIGTERM
and wait. If the launcher does turn out to fight for the screen, fix it in a way
that cannot outlive the process.

**How the launcher actually works** (worth knowing before touching any of it):

- `/etc/inittab` respawns `-/bin/login -f root` on the console.
- That login runs `/root/.profile`, whose last line is `frontend init &`.
- `frontend` is a shell script at `/usr/local/sbin/frontend`; `frontend init` is
  an infinite loop that starts the chosen launcher and restarts it when it exits.
- The choice lives in `$HOME/.frontend`. The loop runs with **`HOME=/root`**, so
  `/root/.frontend` is the file that matters. An SSH session may have a different
  `HOME`, so `frontend set` over SSH can write a file nothing reads.
- `/` is mounted **read-only**, so writing `/root/.frontend` needs
  `mount -o remount,rw /` … `mount -o remount,ro /`.
- `termfix_all` and `keymap` referenced in `NanoWiFi/wifi/fbrun.sh` **do not
  exist** on this device; those calls are silent no-ops.

**The owner runs `gmenu2x`, not retrofe.** (`../CLAUDE.md` says gmenu2x too; an
earlier note in this file claiming retrofe was wrong.)

Recovery if the launcher is ever dead:

```sh
rm -f /mnt/disable_frontend
mount -o remount,rw / && printf 'gmenu2x\n' > /root/.frontend && mount -o remount,ro /
reboot
```

### Debugging on device

`gdbserver` is on the device and the SDK ships `arm-funkey-linux-musleabihf-gdb`.
This is by far the fastest way to diagnose anything:

```bash
# device: attach to an already-running process (more reliable than launching
# under gdbserver, and lets the game settle first)
gdbserver --attach 0.0.0.0:2345 $(pidof pokemon-emerald-nano)
# host:
arm-funkey-linux-musleabihf-gdb -batch \
  -ex "set sysroot ~/funkey-sdk-2.3.0/arm-funkey-linux-musleabihf/sysroot" \
  -ex "target remote 192.168.1.222:2345" -ex "continue" -ex "bt" \
  build/rg-nano/pokemon-emerald-nano
```

Symbols resolve against the **unstripped** `build/rg-nano/pokemon-emerald-nano`
(the shipped one is stripped; build IDs match). Setting breakpoints over the
remote link proved flaky ("Ignoring packet error"); attaching and inspecting
state works reliably. Add `-ex "maint set target-async off"` if `continue`
returns immediately.

---

## What was already here (from the Codex session)

`src/platform/rg_nano.c` (platform backend), `src/platform/secondary_panel*.c`
(240×80 companion panel), `src/platform/rg_nano_asset_gate.c` + `sha1.c`
(ROM-hole gate), `Makefile_rg_nano`, `packaging/rg-nano/`, `scripts/`,
`tools/rg_nano/`, `tests/rg_nano_host_test.c`. The architecture in the original
plan is sound and unchanged. It had **never been run**.

---

## Bugs found and fixed (all on device)

These were found in order; each one hid the next.

### 1. `SDL_Init failed: Unable to open mouse`
The device has only `/dev/input/event0` — no mouse node. SDL 1.2's fbcon driver
aborts `SDL_Init` unless `SDL_NOMOUSE` is set. Drum78OS exports it from
`/etc/profile` line 13, which a non-login `/bin/sh` launcher never sources.
**Fix:** export `SDL_NOMOUSE`/`SDL_VIDEODRIVER`/`SDL_FBDEV` in
`packaging/rg-nano/run.sh`.

### 2. Every pointer in the game-data blob linked as NULL
`Makefile_rg_nano` did `ld -r` then
`objcopy --rename-section .rodata=.data,alloc,load,data,contents`. **objcopy
renames the section but not its companion `.rel.rodata`, then cannot match it up
and silently discards it** — 12,656 relocations dropped. Non-pointer fields
survived, pointers became 0. `gMPlayTable[0].info == NULL` segfaulted
`m4aSoundInit` two seconds after launch.

Renaming to a *unique* name doesn't help either — objcopy drops the relocations
for any renamed section. **Fix:** don't rename. Only the flags need changing, and
`--set-section-flags .rodata=alloc,load,data,contents` keeps the name and
therefore the relocations. A guard in the rule now fails the build if the
relocation count changes across the objcopy.

### 3. The entire soundtrack was missing from the build
With the relocations restored, the link finally surfaced **420 undefined
symbols** (`mus_*`, most `se_*`, all `ph_*`). `sound/songs/` holds ~110
hand-written `.s` files; the other 420 songs are compiled from
`sound/songs/midi/*.mid` by `mid2agb`, driven by per-song options in
`midi.cfg` — which `Makefile_rg_nano` never did. **Fix:** derive the song list
from the `.mid` files and generate the `.s` with the same midi.cfg-driven rule
`audio_rules.mk` uses (inlined, because including that file would collide with
this makefile's own `.bin`/object rules).

### 4. White screen — the VBlank interrupt never ran
`RunVBlank()` gated on `REG_DISPSTAT & DISPSTAT_VBLANK_INTR`. The game enables
VBlank with `EnableInterrupts(INTR_FLAG_VBLANK)`, which updates `REG_IE`
immediately but only reaches `DISPSTAT` through the **buffered** GPU register
manager — and that buffer is flushed by `CopyBufferedValuesToGpuRegs()` from
*inside* `VBlankIntr`. Deadlock: the handler never runs, so the bit that would
enable it is never written.

Symptom: a pure-white 240×160 area (`0xffff` everywhere — just the backdrop
`AgbMain` sets), `gMain.vblankCounter1 == 0` after 20 s, `gMain.state` stuck at
141, and total audio silence (`m4aSoundMain` is called from `VBlankIntr` too).
**Fix:** gate on `REG_IE & INTR_FLAG_VBLANK`, which is exactly what upstream's
Android backend does and for the same reason.

### 5. Segfault loading a blank save
`TryLoadSaveSlot` calls `CopySaveSlotData` **unconditionally**, ignoring
`GetSaveValidStatus`, and that indexes `locations[id]` with `id` read straight
off the flash. Erased flash reads `0xFFFF`, so it dereferenced
`gRamSaveSectorLocations[65535]` (offset `0x7FFFC`). On a GBA that out-of-bounds
read lands harmlessly in the memory map and the signature check discards the
sector; with an MMU it segfaults. **Fix:** bounds-check `id` in
`CopySaveSlotData` and `GetSaveValidStatus` (`src/save.c`). Behaviour is
unchanged — such a sector could never pass the signature check.

---

## Open issues

### Extending the companion panel (next session's work)

Three files, with a deliberate split — keep it:

| file | role |
|---|---|
| `include/platform/secondary_panel.h` | `struct SecondaryPanelModel` — the whole panel state |
| `src/platform/secondary_panel.c` | `SecondaryPanel_Snapshot()` reads game state into the model |
| `src/platform/secondary_panel_render.c` | draws the model into a 240x80 RGB565 buffer |

**The renderer must never read game state, only the model.** The snapshot runs
once per frame while the game thread is parked in `VBlankIntrWait`, so game state
is stable there and nowhere else. This is why there is a model at all.

To add a view:

1. Add a mode to `enum SecondaryPanelMode`.
2. Add its fields to `struct SecondaryPanelModel`.
3. Fill them in `SecondaryPanel_Snapshot()` — mind the ordering in that function:
   battle wins, then a manual `sViewOverride` from L/R, then bag, then the
   180-frame map-on-area-change, then party.
4. Draw it from the `switch` in `SecondaryPanel_Render()`.
5. If it should be reachable with L/R, add it to the `views[]` list in
   `SecondaryPanel_CycleView()`.

Things that will bite:

- **Redraw is revision-gated.** `Snapshot` bumps `revision` only when the model's
  bytes actually change (a `memcmp` from `offsetof(mode)` onward), and
  `DrawComposedFrame` re-renders only when it changes. So *every* field you add
  must be part of the model, or the panel will not repaint when it changes.
  Anything unstable (a timer, a pointer, uninitialised padding) will make it
  repaint every frame -- the whole model is memset before filling, keep it so.
- **Be defensive about game state.** Guard on `IsInGame()`, `gPlayerPartyCount`,
  `gBattlersCount`, `gBagMenu != NULL` before dereferencing. The panel runs
  during boot, cutscenes and the title screen too. The one crash this port has
  seen came from unvalidated game data.
- **Budget.** The panel currently costs **0.21 ms/frame** of a 16.74 ms budget,
  and the frame has roughly 0.5 ms of headroom. Icon decode is already cached by
  id; keep new work out of the per-frame path or cache it the same way.
  `touch <data>/nopanel` measures the cost of the whole panel at any time.
- Bag data is read from the save block, not from runtime pocket pointers, because
  those are not always initialised.

### RESOLVED: the ROM-gated release build

Built and verified on device. Build it with:

```bash
BASEROM="/path/to/verified/rom.gba" DEVELOPMENT_BUILD=0 scripts/package_rg_nano.sh
```

It takes **~30 minutes**: `make_asset_holes.py` searches the 16 MiB ROM for every
data symbol, recursively halving on a miss, and any source change alters the
build id and every address, so the whole pass must be redone. A progress line is
printed to stderr. Results:

| | |
|---|---|
| ROM data identified | 10.4 MB (62% of candidate data) |
| zeroed from the executable | 9.0 MB |
| manifest entries | 15837 |
| OPK size | 10.2 MB dev -> **5.44 MB** release |

Verified by sampling 300 random 64-byte ROM chunks: **230/300 present in the
development binary, 6/300 in the release binary**. The remaining few are most
likely incidental byte patterns or blobs under the tool's 32-byte `MIN_SIZE`;
that has not been chased down, so do not claim the binary is provably clean.

Both OPKs are archived: `dist/release/` (with its `asset_manifest.bin`) and
`dist/dev/` as a known-good fallback. **The manifest and the binary are a matched
pair** -- shipping a manifest from a different build fails the gate.

All four gate paths are confirmed on hardware:

| case | screen |
|---|---|
| valid ROM | none -- `[Assets] ROM ASSETS READY`, game runs |
| ROM absent | `BASEROM.GBA MISSING` + where to put it |
| 16 MiB file, wrong SHA-1 | `WRONG EMERALD ROM` |
| manifest from another build | `BUILD ID MISMATCH` |

**The bug that made the gate impossible on this device:** `FindExecutable` picked
the loaded object with an empty `dlpi_name`. That is a glibc convention -- musl
reports the main executable by its full path, so the only unnamed object is the
**vDSO**, which carries its own GNU build-id note. The gate compared the
*kernel's* build id against the manifest and reported `BUILD ID MISMATCH` on
every launch. It now identifies its own image by finding the object whose
`PT_LOAD` ranges contain the address of a function in the gate itself, which is
independent of naming conventions. If you touch this code, verify it with a
standalone probe on device (`dl_iterate_phdr` printing every object) rather than
by rebuilding -- a rebuild costs 30 minutes.

Two gate-screen quirks were fixed at the same time: the `A RETRY   B EXIT` prompt
was passed as message line 3 *and* drawn by `SecondaryPanel_RenderFullScreen`, and
`RgNanoAssetGate_Fill` echoed the result text into `detail` when there was no
specific detail, so the error printed twice. The failure line is now yellow.

### RESOLVED: audio silence, audio crackle, and the frame budget

All three are fixed. The chain of causes, because none were what they looked
like:

1. **Silence.** `Platform_QueueAudio` was never called: the V-count interrupt
   that drives `m4aSoundVSync` never fired, because `UpdateRegDispstatIntrBits`
   (`gpu_regs.c`) wrote `newValue` as the *whole* DISPSTAT register while only
   accounting for the V-blank/H-blank enable bits, wiping the compare line and
   `DISPSTAT_VCOUNT_INTR` that `EnableVCountIntrAtLine150` sets once at boot.
   Fixing it needed a second insight: `GetGpuReg(REG_OFFSET_DISPSTAT)` reads the
   *live* register while `SetGpuReg` writes a shadow buffer flushed at the next
   V-blank, so merging from the live register still discarded the staged value.
   It now merges into the pending buffered value. Confirmed by logging
   `dispstat`: `0008` before, `9628` after.
2. **A permanent ~5% dropout independent of frame rate.** SDL reported granting
   42060 Hz, but the hardware drains ~44100. Derived from the counters: 841200
   samples queued + 65418 underruns per 600 frames only balances at 44100. The
   device is now opened at 44100 and `Platform_QueueAudio` does a **fixed-ratio**
   sample rate conversion (ratio from two constants -- an earlier *adaptive*
   version bent the pitch audibly and was rightly rejected).
3. **The rest was frame rate.** The mixer emits one buffer per game frame, so
   audio is short exactly in proportion to missed frames. See below.

**Do not "fix" audio with buffering, priming or rate feedback.** Those were all
tried and all made it worse or wobbled the pitch. If audio degrades, look at the
frame rate first -- the two are rigidly coupled by design.

### Frame budget and redraw skipping

A real GBA frame is 228 scanlines x 1232 cycles at 16.777216 MHz = **59.7275 Hz
/ 16.7427 ms**, not 60 Hz / 16.6667 ms; pacing to the latter counted frames late
that hardware would have made. `RG_NANO_FRAME_NS` is the authentic value.

The renderer went from 17.8 ms to ~13.7 ms per frame:

| change | effect |
|---|---|
| per-tile instead of per-pixel BG rendering | bg 10.4 -> 8.8 ms (biggest win) |
| `restrict` on the layer buffer | bg -12% (see note below) |
| compositor skips sprite priorities with nothing on the line | composite 4.2 -> 3.2 ms |
| hoist volatile `REG_WININ`/`REG_WINOUT` out of the pixel loop | win 0.99 -> 0.45 ms |
| BGR555->RGB565 by arithmetic, not a 64KB LUT | conv 0.82 -> 0.42 ms |

That `restrict` matters because the build uses `-fno-strict-aliasing`: without
it the compiler assumes each store into the layer buffer may have modified VRAM
or the palette and reloads both every pixel.

Even so the frame landed at ~17.5 ms against a 16.74 ms budget. The last ~0.8 ms
is bought by **skipping the redraw, never the frame**: `gSkipPixelRender` makes
`DrawFrame` walk every scanline and fire `REG_VCOUNT`, the V-count interrupt, the
H-blank DMAs and the H-blank interrupt exactly as normal, but produce no pixels,
and the platform layer then skips the conversion and the page flip too. Game
logic, timers and audio stay bit-exact at 60 Hz; only the picture is not
refreshed that once. It triggers only when a full frame behind and never twice
running. Measured: **5 skips per 600 frames (0.8%), 3 late frames per 600, and
zero audio underruns in steady state.**

Things that were tried and measured *worse*, so don't repeat them: lazy sprite
row clearing (moved the same blanking into `obj` and cost more), paired 32-bit
framebuffer stores (the LUT loads were the bottleneck, not the stores), and
`-mcpu=cortex-a7 -mfpu=neon-vfpv4` (no measurable change; the renderer is
memory-bound, not FP-bound -- the flag is kept only as a correct target
description).

Useful hardware facts, measured on device with a small benchmark:
CPU is **not** throttled (~1.1 GB/s sequential reads); scattered 4-byte reads
cost ~95 ns each (a DRAM miss per tile fetch); the display is `fb_st7789v`,
**75 MHz SPI, rated 100 fps, async with 3 back buffers**, so the panel is *not*
a bottleneck at 60. The companion panel costs **0.21 ms/frame** -- measured by
`touch /mnt/FunKey/.pokemon-emerald-nano/nopanel`, which disables it -- so it is
not worth removing.

### A. (historical) Performance — was the blocker
**~83 % of frames miss the 20 ms budget** against a ≥59 FPS / <1 % late gate.
This is now measured, not guessed. `rg_nano.c` reports a per-phase breakdown on
every `[Perf]` line, and `PROFILE_PPU=1 make -f Makefile_rg_nano` adds a `[PPU]`
line splitting the renderer itself:

| phase | per frame | note |
|---|---|---|
| **ppu** | **13–15 ms** | the entire problem |
| ├ bg | 7.8–10.1 ms | BG scanline rendering — the hot spot |
| ├ composite | 2.7–3.1 ms | per-priority layer merge |
| ├ obj | 1.1–3.3 ms | sprites |
| └ win | 0.19 ms | negligible |
| vblank | 0.95 ms | incl. the audio mixer — **not** a problem |
| conv | 0.7 ms | BGR555→RGB565, negligible |
| flip | 0.8 ms | `SDL_Flip` does **not** block on the panel |
| stalled | 0 | the game thread is never the bottleneck |

Two things already tried, both honestly disappointing:
- **Cortex-A7/NEON flags** (`-mcpu=cortex-a7 -mtune=cortex-a7 -mfpu=neon-vfpv4`,
  now in `TARGET_ARCH_FLAGS`): **no measurable change.** The PPU is integer and
  memory bound, not FP. Kept because it is the correct target description, but
  do not expect anything from it.
- **Loop-invariant hoisting + narrower per-scanline clears** in
  `gba_easy_draw.c`: **~4 % off `bg`.** Worth keeping, not the answer.

**The real remaining lever is `RenderBGScanline`.** It is a naive per-pixel loop:
for every one of the 240 pixels it recomputes the map entry
(`bgmap[mapRowOffset + xx / 8]`), the tile number, palette number, flip bits and
tile address — even though **8 consecutive pixels almost always share one tile
entry**. Restructuring it to fetch the tile entry once per 8-pixel span and emit
the run (with the 4bpp nibble pairs unpacked two at a time) should cut the
dominant cost by something close to a factor of 8 on the lookup half. Mind the
edge cases that make it fiddly: mosaic, H/V flip, 512 px maps crossing screen
bases mid-row, and the `gRenderMargin` clipping.

Note `line[i]` is written **only when `pixel != 0`** in 4bpp, so transparent
pixels depend on the layer having been cleared — do not "optimise away" the
per-scanline `memset`.

**Do not** reduce gameplay resolution or frame rate to hit the gate. Frame
skipping (rendering every other frame while still ticking the game at 60 Hz) is
the obvious escape hatch but is explicitly out of bounds per the original plan —
raise it with the owner rather than doing it unilaterally.

### B. No audio
`audio_under` is still ≈ every sample. This is **probably a symptom of A**: the
mixer produces exactly one frame of samples per game frame, so at ~40 FPS it
generates well under what the 44.1 kHz device consumes and starves permanently.
The profiling supports that reading — `vblank`, which contains the whole mixer,
costs only 0.95 ms, so the mixer is running and is not slow; it is simply not
being run often enough. Fix performance first, then re-measure.

If it persists: `Platform_QueueAudio` is only ever called from `m4aSoundVSync`
(`src/music_player.c`), which runs from `VCountIntr` = `gIntrTable[0]`, which is
fired by the software PPU in `gba_easy_draw.c` when `REG_VCOUNT` reaches the
DISPSTAT vcount setting (150). That gate reads `REG_DISPSTAT`, which only became
reachable after fix #4 — so verify it now actually fires. The stubs in
`src/stub.c` are **not** the problem: the m4a ones sit inside the commented-out
block spanning lines 113–221 (verified with `nm` — `stub.o` defines no m4a
symbols).

### C. Verified on hardware

Controls, sound, saves across relaunches, the battle UI, and the companion panel
switching with game state are all confirmed by the owner. The release build and
all four asset-gate paths are confirmed too (see above).

L and R do **not** send GBA shoulder buttons: they cycle the companion view
(automatic -> party -> map). Emerald barely uses them, and the owner asked for
them to drive the lower screen. If a menu ever needs the real L/R, put them back
behind a modifier rather than reclaiming the buttons.

Still unverified: the MENU (`q`) exit-confirmation flow, and the bag/map panels
in ordinary play.

### D. Logging

A release run writes ~6 lines to `last.log`. Per-frame timing and audio counters
are opt-in:

```bash
touch /mnt/FunKey/.pokemon-emerald-nano/debug    # [Perf]/[Loop]/[Audio] on
touch /mnt/FunKey/.pokemon-emerald-nano/nopanel  # run without the companion panel
```

`PROFILE_PPU=1 make -f Makefile_rg_nano` additionally breaks the renderer down
per stage. Those counters are what made the audio and frame-rate faults
findable; keep them.

### E. Device clock

There is no `synctime`, `ntpd` or `rdate` on this firmware and no battery-backed
RTC, so the clock resets on a full power-off and Emerald's real-time events
(berry growth, tides, lottery) misbehave until it is set. Set it from a host:

```bash
./nano_remote.sh run "date -s \"$(date '+%Y-%m-%d %H:%M:%S')\""
```

Worth folding into `run.sh` at some point.

### F. Open

- **One unreproduced crash on entering a battle.** A handler for SIGSEGV/BUS/
  ILL/FPE/ABRT logs `[Crash] signal= addr= pc= lr=` to `last.log`; map it with
  `arm-funkey-linux-musleabihf-addr2line -fe build/rg-nano/pokemon-emerald-nano`.
  Keep the matching unstripped ELF for whatever build is on the device.
- **6 of 300 sampled 64-byte ROM chunks still appear in the release binary.**
  Probably incidental byte patterns or blobs under the tool's 32-byte `MIN_SIZE`,
  but that has not been confirmed, so the binary is not *provably* free of ROM
  data.
- `audio_over` was seen non-zero (954 in one session): the ring occasionally
  fills. Harmless so far, but it means production slightly exceeds consumption
  at times.

---

## Build-system traps

- **`.SECONDARY:` with no prerequisites makes every target intermediate.** If an
  intermediate like `generated/game_data.o` is deleted, make will *not* rebuild
  it as long as the final ELF exists — it silently reports success and links
  nothing new. Two of my "rebuilds" were no-ops because of this. Force it:
  `make -f Makefile_rg_nano build/rg-nano/generated/game_data.o` then delete the
  ELF. Worth fixing properly by scoping `.SECONDARY:` to real intermediates.
- The generated `sound/songs/midi/*.s` are written into the **source tree**
  (upstream does this too). `make -f Makefile_rg_nano clean` only removes
  `build/`, so they persist; `rm -f sound/songs/midi/*.s` to force regeneration.
- Passing `$VAR` through `wsl.exe bash -c '...'` from PowerShell: PowerShell
  expands `$VAR` before bash sees it, even inside single quotes. **Write shell
  scripts to files and run those**; don't inline non-trivial shell.

## Note on the linker output

Making `.rodata` writable collapses the ELF into a single RWE `PT_LOAD` segment
(~20 MB). It works, and RSS on device is fine, but it is worth revisiting whether
the game-data blob needs to be writable at all — `Makefile_pc` links these
objects as plain read-only `.rodata`, and the asset gate `mprotect`s pages
anyway. Read-only would restore separate `R E` / `RW` segments and keep those
pages clean/evictable. Low risk, untested, and unrelated to any current bug.
