# experiments/fullscreen240

**Question:** can the RG Nano's whole 240×240 panel show the game — no stretch,
no letterbox — instead of a 240×160 game area plus the 240×80 companion panel,
with the dialogue box moved down to the new bottom edge?

**Answer: yes at 2×, and nearly at 1:1** — the 1:1 view turned out to be capped
at 208 of the 240 rows by the game's own map-buffer maintenance, so it keeps a
16px bar top and bottom.

| mode | what you see | frame rate |
|---|---|---|
| **2× zoom** (default) | 120×120 of game space magnified 2× — uniform pixels, fills the screen | **60 fps**, ~7 ms of headroom |
| **1:1** (Y toggles) | 240×208 of game space — 24 rows of extra world above and below, letterboxed | **48 fps** |

2× is the mode to play in. 1:1 is the "see more world" option, and it is capped
at **208 rows, not 240** — see [the popping](#the-popping-npcs-and-tiles-what-it-actually-was),
which is the most useful thing this experiment found.

Getting 1:1 to 60 fps was investigated and **abandoned deliberately** — see
[Why 1:1 cannot reach 60](#why-11-cannot-reach-60).

---

## Measured on hardware

The renderer costs **0.357 µs per rendered pixel**, and that held to three
digits across two very different frame sizes — 240×160 at 13.7 ms and 240×240
at 20.6 ms. Everything else here follows from it.

| | rendered px | predicted PPU | measured PPU | redraws skipped | fps |
|---|---|---|---|---|---|
| stock 240×160 + panel | 38,400 | 13.7 ms | 13.7 ms | 0.8 % | ~59.5 |
| **2× (120×120 source)** | 14,400 | 5.1 ms | 6.2 ms | **0.8 %** | **60** |
| 1:1 at 240 rows (artefacts) | 57,600 | 20.6 ms | 20.5 ms | 29.5 % | 43 |
| **1:1 at 208 rows (shipped)** | 49,920 | 17.8 ms | **17.7 ms** | **19.8 %** | **48** |

The pixel-linear model predicted the 208-row figure to within 0.5 % before it
was measured, which is the third time it has held. Measured PPU is the
drawn-frame cost, recovered from the logged average and the skip rate.

2×'s skip rate is *identical to the stock build's*, which is what makes "60 fps"
a measurement rather than a claim.

What 1:1 keeps perfectly, even below 60, is game timing. `gSkipPixelRender`
drops the redraw and never the frame, so `vcount_fires` and `queue_calls` were
600/600 in every sample and `audio_under` froze at its startup value and never
moved again. The cost is paid purely in visual smoothness; the game runs and
sounds exactly as it always did.

---

## How it works

One scalar drives everything: **`scale`, in percent**. The source region is
`24000 / scale` pixels square, centred on the GBA viewport, and the composition
step magnifies it onto the 240×240 panel.

| scale | source | what it does |
|---|---|---|
| 100% | 240×208 | 1:1, letterboxed. Reveals 24 rows above and below the viewport — the map buffer's whole budget, see below. |
| 200% | 120×120 | Magnifies 2× to fill the panel. Crops both ways. Uniform pixels. |

Source region and output are both square, and GBA pixels are square, so **every
scale is a uniform magnification** — nothing is ever stretched. Above 100% the
margins go *negative*, and a negative margin is simply a crop: buffer column 0
holds game column `-gRenderMargin` either way, so widening and cropping are the
same arithmetic with opposite signs.

Game coordinates, collision, and every gameplay rule stay in the 240×160 space
they always were. Only the *view* changes.

The geometry is the vertical twin of the port's existing widescreen support
(`gRenderWidth` / `gRenderMargin` in `src/platform/gba_easy_draw.c`, which
renders extra columns outside the viewport and reveals map the game has already
scrolled past):

| new global | meaning |
|---|---|
| `gRenderTopMargin` | rows rendered above game-space row 0 (negative = cropped) |
| `gRenderBottomMargin` | rows rendered below game-space row 159 |
| `gRenderHeight` | rows the platform layer scales onto the panel |

### 1.25× and 1.5× were built, tried, and cut

Both hit 60 fps comfortably (36,900 and 25,600 pixels), and 1.5× had a nice
property: its 160-row source is exactly the full GBA height, so it needs no
margin rows at all. They were removed anyway because they are *non-integer*
scales — 1.25× doubles every 4th pixel and 1.5× every 2nd — and on this panel
the owner's verdict was that they "look very bad". 2× is the only magnification
with uniform pixels.

### Two separate caps on how far 1:1 can reach

`DrawWholeMapViewInternal` (`src/field_camera.c`) fills all 16 metatile rows of
the overworld's 32-tile-tall BG tilemap — 256px of real map — of which the GBA
viewport shows 160. The spare 96px sits as **40px above and 56px below** the
viewport. That is the cap on *content*: ask for more than 40 above and you get
the tilemap's wraparound instead of map.

But content is not the binding constraint. The **maintenance strips are**, and
they cap it at 24 each way — see below. 40 was tried first and is exactly what
produced the popping.

### Where the margin rows are drawn

They sit **outside the GBA's scanline timing**. They get no `REG_VCOUNT`, no
H-blank DMA and no H-blank interrupt of their own, because any of that would
change game timing — the one thing this must not do. The top rows are drawn from
the register state that holds at scanline 0, the bottom rows from the state left
after scanline 159. Consequence: anything driven per-scanline by an H-blank DMA
(wobble effects) reads flat across the margins.

### The popping NPCs and tiles: what it actually was

**This is the finding to keep.** A 240×240 view at 1:1 is not achievable on this
game, and the reason is structural rather than a bug that can be fixed.

The overworld's BG tilemap is a **256px-tall circular buffer**, and
`RedrawMapSliceNorth`/`South` (`src/field_camera.c`) rewrite one metatile row of
it every time the camera crosses a 16px boundary. Every 2D scrolling engine does
this, and every one of them relies on that strip being outside the viewport —
the standard advice is to put the seam as far from the view as possible. The
retail 240×160 view leaves 40px of slack above it and 56px below, so it always
is.

Extending to the full 240 rows ate that slack and put the view directly on top
of **both** strips. The hardware report is unambiguous and the arithmetic is
exact:

> tiles *and* NPCs blinking in the outermost **two tile rows** at each edge

Two tile rows is 16px. One metatile row is 16px. That is the strip.

It explains the NPCs too, without any separate cause: the object load window's
top and bottom boundaries (`pos.y` and `pos.y + 16`) sit in those same rows, and
spawn/removal runs on the same camera-step event as the tilemap rewrite. Both
kinds of artifact appear together, in the same 16px, for the same reason — which
is why nothing done to the object window alone ever helped.

**So the artifact-free 1:1 view is 160 + 24 + 24 = 208 rows.** Pull in by one
metatile row at each edge and the maintenance strips are hidden again. The
remaining 32 rows are letterboxed, not scaled: 240/208 is a non-integer
magnification and those were already rejected on this panel. `margin=24` in the
config is that budget, and there is no more to spend — raising it brings the
strips straight back. Getting the full 240 would mean making the BG map 64 tiles
tall (4 screenblocks per layer, 3 layers), which is a different project.

### The object-window fix that made it worse

Before the above was understood, the NPC half was diagnosed on its own:
`TrySpawnObjectEvents` and `RemoveObjectEventIfOutsideView` use
`top = gSaveBlock1Ptr->pos.y` — **zero** vertical slack, where the horizontal
check was already given 5 metatiles of it for widescreen. True as far as it
goes. **The fix was wrong anyway**, and the way it failed is worth keeping:

> Slack was added *above and below*, scaled from the margin plus the same
> 3-metatile lead the horizontal check uses. Result on hardware: *more* NPCs
> popping, and now at the **bottom** edge too, where they never had before.

The load window is not free. There are only **`OBJECT_EVENTS_COUNT` = 16 object
slots for the whole map**, and `TrySpawnObjectEventTemplate` simply fails when
they are gone. The retail window is 17×17 metatiles; upstream's widescreen
`extraX` already stretched it to 27×17 = 459 candidates for those 16 slots.
Adding 6 rows each way took it to 27×29 = **783**, so which templates got slots
churned as the camera moved and NPCs flickered *everywhere* rather than at one
edge. Widening the window past the visible area does not buy safety — it trades
edge pop-in for global pop-in.

`ExperimentVerticalObjectSlack()` is now the **minimum**: `ceil(topMargin / 16)`
metatiles above only, which puts the boundary just past the top of the drawn
area and no further, and nothing added below. That is 20 rows rather than 29.

An `[Objects] peak_active=N/16` line now goes out with the other per-600-frame
counters, so slot pressure can be *seen* rather than inferred. It read **4/16**
throughout the area where the fixed build was tested, so slots were never the
constraint there — the earlier "worse everywhere" report came from a town with
many more NPCs, and whether it was really slot exhaustion is unproven.

The line originally also carried a `spawn_failures` counter. It was removed
because it was **wrong**: `TrySpawnObjectEventTemplate` returns
`OBJECT_EVENTS_COUNT` both when the table is full and when the object is simply
already spawned — `GetAvailableObjectEventId` cannot tell those apart — so it
reported tens of "failures" per second on a map with four NPCs and twelve free
slots. A number that cannot distinguish the thing you are hunting from the normal
case is worse than no number.

At 2× there are no vertical margins at all, so none of this can happen there.

### Debug knobs left in

`margin=N` sets the 1:1 margins (default 24, the map buffer's budget). `top=N`
splits the extra rows unevenly without changing the total, for probing which
edge an artifact belongs to. Both are config-file only, so testing them needs a
relaunch but no rebuild.

### Zooming crops the field UI, so the view pulls back

The dialogue box is 232px wide and the start menu sits hard against the right
edge, so any zoom cuts them off — and the box cannot simply be made narrower,
because Emerald's messages carry their line breaks inside the strings. Instead
`FieldUiOpen()` eases the view back to 1:1 (25 percent points per frame, about
four frames) whenever any of it is on screen, then zooms back in afterwards. At
1:1 everything fits, and a static dialogue does not care that 1:1 is the 43 fps
mode. Confirmed on hardware: "the transition and animation when at 2× when
talking to an NPC and walking into buildings to 1× works perfectly."

**Do not gate this on `gMenuCallback`.** It is set to `HandleStartMenuInput`
when the start menu opens and **never cleared** when it closes — the task is
destroyed but the pointer keeps its stale value for the rest of the process.
Testing it latched the view at 1:1 from the first press of START onwards, with
no way back short of restarting the game. `FieldUiOpen()` uses
`GetStartMenuWindowId()` and `ArePlayerFieldControlsLocked()` instead, both of
which the game clears itself (`RemoveStartMenuWindow`,
`UnlockPlayerFieldControls`).

### Scope: the field only

Battles, menus, the title screen and every cutscene are laid out for a 240×160
screen and their BG maps hold nothing useful outside it, so they keep the stock
frame and the companion panel. `Fullscreen240_Update()` decides once per frame,
at the top of `DrawComposedFrame` — the only point where the game thread is
parked and the geometry can change without tearing a frame between two sizes.

### The dialogue box

The standard text box sits at tile rows 14…19 (game y 112…160): the bottom of
the GBA viewport, but the *middle* of the 1:1 widened frame. `menu.c` pushes the
field's copy down 5 tile rows, landing it at game y 152…200 — the bottom edge —
using tilemap rows the 32-row BG0 map already has. Measured off the framebuffer:
the frame artwork lands on screen rows 195…236, exactly where it should, the few
pixels below it being the dialogue frame's own inset. The field yes/no box moves
with it. Only the field's copy moves: the naming screen, hall of fame and the
minigames call the same init function but draw into a 240×160 frame.

### The exit prompt

MENU already worked but *invisibly*: the confirmation is drawn by the companion
panel, which this experiment hides. Pressing it halted the game waiting for an
A/B the player could not see. The panel now comes back over the bottom 80 px
whenever `sExitConfirmation` is set, whatever the render geometry.

---

## Why 1:1 cannot reach 60

A 16.74 ms frame buys about 39,000 rendered pixels. 1:1 renders 57,600. Closing
that means removing a third of a renderer that has already had one serious
optimization pass, with several measured-worse attempts on record in
`RG_NANO_PORT_HANDOFF.md`.

One optimization was tried here and **it did not work**: skipping wholly
transparent 4bpp tile rows. The reasoning looked sound — BG0 carries only text
and is blank across almost every field scanline — but it moved the drawn-frame
PPU from 20.65 ms to 20.48 ms, under 1 %. The cost is not the arithmetic it
removes, it is the two *scattered memory reads* that come first: the tilemap
entry and the tile row, each around 95 ns on this hardware (a DRAM miss per
fetch, per the handoff's own benchmark). You cannot skip the fetch, because
reading the tile row is the only way to learn it is blank.

The change was kept anyway — it is free and byte-identical — but nobody should
expect frame rate from it. Getting 1:1 to 60 means restructuring
`RenderBGScanline` so tilemap and tile fetches are sequential rather than
scattered. That is a project, and it was consciously not taken on here.

---

## Controls added by this build

| button | effect |
|---|---|
| **X** | toggle the experiment off/on (instant A/B against the stock 240×160 + panel) |
| **Y** | toggle zoom: 2× ↔ 1:1 |

Neither is a GBA button (see `KeyMask` in `src/platform/rg_nano.c`), so nothing
is taken from the game. The text box position is decided at map load and is
deliberately *not* tied to the current zoom — the view is always back at 1:1 by
the time a box is drawn.

## Config: `<data dir>/fullscreen240.cfg`

```
enabled=1     # start with the experiment on
zoom=200      # default zoom: 200 (2x) or 100 (1:1)
textbox=1     # move the field dialogue box down to the new bottom edge
```

## Data dir

`/mnt/FunKey/.pokemon-emerald-fs240` — its own, seeded from a copy of the real
save on first launch. **The experiment cannot corrupt the real save.**

---

## Build / deploy / look

All from WSL Ubuntu:

```bash
cd /mnt/c/Programmering/SBC/RG_Nano/Pokemon_Emerald_RG_Nano
experiments/fullscreen240/build.sh     # -> dist/PokemonEmeraldFS240_funkey-s.opk
experiments/fullscreen240/deploy.sh    # -> /mnt/Native games/ on the device
experiments/fullscreen240/capture.sh experiments/fullscreen240/shots/name
```

`capture.sh` dumps the framebuffer and converts it to PNG (`fb2png.py`, pure
stdlib — PIL is not installed here). The device has no `base64`, so it `dd`s to
tmpfs and pulls the raw bytes over ssh. **Capture while the app is still
running**: after it exits the launcher repaints its wallpaper over the top,
which is easy to mistake for game output.

A new OPK does not appear in gmenu2x until it rescans, so after the *first*
deploy exit to the launcher and restart it (`pkill gmenu2x`; the frontend loop
brings it back). Replacing an OPK that is already listed needs no restart.

`touch <data dir>/debug` turns on the `[Perf]`/`[Loop]`/`[Audio]` counters that
every number in this document came from.

### The build shares `build/rg-nano` with the stock build

Deliberate — a separate build dir means recompiling the whole game plus the 420
generated songs. Consequences:

* After running `build.sh`, the ELF in `build/rg-nano` is the *experiment's*.
  Rebuild without the flag (`make -f Makefile_rg_nano`) to get the stock one back.
* The stock OPKs are archived in `dist/dev` and `dist/release` and are untouched.
* `baseline-unstripped.elf` next to this README is a copy of the unstripped ELF
  matching the **release build that was on the device**, kept so a crash in that
  build can still be symbolicated with `addr2line`.
* `make` cannot see that `-DRG_NANO_FULLSCREEN` changed, so `build.sh` touches
  every source that reads the flag before building. If you add another, add it
  to that list.

## Files touched outside this directory

All behind `#if RG_NANO_FULLSCREEN` — **with one deliberate exception**: the
blank-tile-row skip in `RenderBGScanline` is unguarded, because it produces
byte-identical output and applies to every build. (It is also, as measured
above, worth almost nothing.)

| file | change |
|---|---|
| `Makefile_rg_nano` | `RG_NANO_FULLSCREEN=1` adds the define and this directory's source |
| `include/platform.h` | `gRenderHeight` / `gRenderTopMargin` / `gRenderBottomMargin`, and no-op macros when off |
| `src/platform/gba_easy_draw.c` | margin scanline passes in `DrawFrame`, signed scanline numbers, crop clamp, vertical window extension, sprite y-wrap, blank-tile skip |
| `src/platform/rg_nano.c` | taller frame buffer, per-frame geometry update, nearest-neighbour scale onto the panel, exit prompt over the widened frame, X/Y keys |
| `src/event_object_movement.c` | vertical NPC cull and spawn window follow the margins |
| `src/menu.c`, `include/menu.h`, `src/overworld.c` | field dialogue box + yes/no box move down |

---

## Known limitations

1. **You see less world at 2×, not more.** 120 of the GBA's 240 columns and 120
   of its 160 rows. That is inherent to zooming and is the price of 60 fps —
   the opposite of what the 1:1 mode does.
2. **Leaving the field pops.** A battle, a menu, or the title screen snaps
   straight back to the stock 240×160 + panel with no transition.
3. **Sprites parked below the viewport.** `DrawSprites` used to treat any OAM
   y ≥ 160 as a sprite entering from the top; it now only wraps y ≥ 192.
   Anything the game hides by parking it at y 160…191 would become visible in
   the bottom margin at 1:1.
4. **Windows.** Full-screen window bounds are stretched across the margins the
   same way the horizontal ones are. Interior windows (the cave flash circle,
   battle transitions) keep their exact coordinates, so a flash circle will not
   cover the margins.
5. **Menus positioned relative to the text box** (multichoice lists in
   `script_menu.c`) have not been moved. They float where they always did,
   which at 1:1 means above the relocated box.
6. **`audio_over` climbed steadily** (~1 per frame) through one long 1:1
   session, while `audio_under` stayed frozen. It was zero in later sessions and
   nothing was audible, so it was not chased — but the handoff already lists a
   non-zero `audio_over` as an open question, and this is a second sighting.
