// experiments/fullscreen240 -- see README.md in this directory.
//
// Decides, once per frame, what region of GBA screen space the software PPU
// should render so that it fills the 240x240 panel. Nothing here draws: it only
// sets gRenderWidth / gRenderMargin / gRenderHeight / gRenderTopMargin /
// gRenderBottomMargin, which src/platform/gba_easy_draw.c renders into and
// src/platform/rg_nano.c scales onto the screen.
//
// Called from the platform layer at the top of DrawComposedFrame, which runs
// while the game thread is parked in VBlankIntrWait -- the only point where
// game state is stable. Same rule as SecondaryPanel_Snapshot.
//
// One scalar drives everything: `scale`, in percent.
//
//   source region = 24000 / scale, square, centred on the GBA viewport
//   100% -> 240x240 source, shown 1:1. Reveals 40 rows above and below the
//           viewport, which is exactly what the overworld's BG tilemap holds.
//   150% -> 160x160 source, magnified 1.5x. Exactly the full GBA height, so no
//           margin rows at all -- and 40 columns cropped from each side.
//   200% -> 120x120 source, magnified 2x. Crops in both directions.
//
// Both the source region and the output are square and GBA pixels are square,
// so every scale is a uniform magnification. Nothing is ever stretched.
//
// Why zoom at all: the renderer costs a measured 0.357us per rendered pixel, so
// 60fps buys about 39000 of them. 240x240 at 1:1 is 57600 -- 43fps, which is
// what the first version of this experiment ran at. Zooming renders *fewer*
// pixels, so it fills the screen and hits 60 at the same time.

#if RG_NANO_FULLSCREEN

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "field_message_box.h"
#include "main.h"
#include "overworld.h"
#include "platform.h"
#include "menu.h"
#include "script.h"
#include "window.h"

#include "fullscreen240.h"

#define SCALE_ONE_TO_ONE 100
#define SCALE_MAX        200

// Y cycles these. 125% and 150% were tried on hardware and cut: a non-integer
// scale doubles some pixels and not others, and on this panel that reads as
// mush. 200% is the only magnification with uniform pixels, so it and 1:1 are
// the two that survived.
static const int sZoomSteps[] = { 200, 100 };

// Percent points per frame while zooming. 25 crosses the whole 100..200 range
// in four frames, which is quick enough that a dialogue box never appears
// mid-zoom but slow enough to read as a camera move rather than a cut.
#define SCALE_STEP 25

// The overworld's BG tilemap is a 256px-tall circular buffer, and
// RedrawMapSliceNorth/South (src/field_camera.c) rewrite one metatile row of it
// every time the camera crosses a 16px boundary. Scrolling tile engines rely on
// that strip being outside the viewport; the retail 240x160 view leaves 40px of
// slack above it and 56px below, so it always is.
//
// Extending the view to the full 240 rows at 1:1 ate that slack and put the
// view directly on top of both strips. Confirmed on hardware: tiles *and* NPCs
// blinking in the outermost **two tile rows** at each edge -- 16px, exactly one
// metatile row, exactly the strip. The object load window's top and bottom
// boundaries sit in the same rows, which is why both kinds of artifact appeared
// together and why nothing done to the object window alone ever helped.
//
// So the artifact-free 1:1 view is 160 + 24 + 24 = 208 rows, not 240: pull in
// by one metatile row at each edge and the maintenance strips are hidden again.
// The remaining 32 rows are letterboxed rather than scaled -- 240/208 is a
// non-integer magnification and those were already rejected on this panel.
//
// Raising this past 24 brings the strips back into view. That is the whole
// budget; there is no more slack to spend without making the BG map taller.
#define DEFAULT_MARGIN 24

// Diagnostic knob for the popping along the top edge. At 1:1 the 80 extra rows
// are split evenly, 40 above and 40 below; this moves the split without
// changing the total, so the frame stays exactly 240 rows and exactly 1:1.
// Sliding rows from the top to the bottom says whether the artifact belongs to
// the top edge of the map buffer: if it follows the margin it does, if it stays
// put it does not. 0..80; -1 leaves the even split alone.
static int sTopSplit = -1;
static int sMargin = DEFAULT_MARGIN;   // rows revealed each way at 1:1

static bool32 sEnabled = TRUE;      // the X-button toggle
static bool32 sShiftTextBox = TRUE; // move the dialogue box to the new bottom
static int sZoomIndex;              // index into sZoomSteps
static int sScale = SCALE_ONE_TO_ONE;
static bool32 sActive;              // widened frame live as of this frame
static char sConfigPath[512];
static char sStatus[160];

static void ReadConfig(void)
{
    FILE *file;
    char line[128];

    if (sConfigPath[0] == '\0')
        return;
    file = fopen(sConfigPath, "r");
    if (file == NULL)
        return;
    while (fgets(line, sizeof(line), file) != NULL)
    {
        int value;

        if (sscanf(line, "enabled=%d", &value) == 1)
            sEnabled = (value != 0);
        else if (sscanf(line, "textbox=%d", &value) == 1)
            sShiftTextBox = (value != 0);
        else if (sscanf(line, "margin=%d", &value) == 1)
        {
            if (value < 0)
                value = 0;
            if (value > (MAX_RENDER_HEIGHT - DISPLAY_HEIGHT) / 2)
                value = (MAX_RENDER_HEIGHT - DISPLAY_HEIGHT) / 2;
            sMargin = value;
        }
        else if (sscanf(line, "top=%d", &value) == 1)
        {
            if (value < 0)
                value = 0;
            if (value > MAX_RENDER_HEIGHT - DISPLAY_HEIGHT)
                value = MAX_RENDER_HEIGHT - DISPLAY_HEIGHT;
            sTopSplit = value;
        }
        else if (sscanf(line, "zoom=%d", &value) == 1)
        {
            int i;

            for (i = 0; i < (int)ARRAY_COUNT(sZoomSteps); i++)
            {
                if (sZoomSteps[i] == value)
                    sZoomIndex = i;
            }
        }
    }
    fclose(file);
    sScale = sZoomSteps[sZoomIndex];
}

void Fullscreen240_Init(const char *dataDir)
{
    snprintf(sConfigPath, sizeof(sConfigPath), "%s/fullscreen240.cfg", dataDir);
    ReadConfig();
    fprintf(stderr, "[FS240] %s\n", Fullscreen240_StatusLine());
}

void Fullscreen240_Toggle(void)
{
    sEnabled = !sEnabled;
    fprintf(stderr, "[FS240] %s\n", Fullscreen240_StatusLine());
}

void Fullscreen240_CycleZoom(void)
{
    sZoomIndex = (sZoomIndex + 1) % (int)ARRAY_COUNT(sZoomSteps);
    fprintf(stderr, "[FS240] %s\n", Fullscreen240_StatusLine());
}

const char *Fullscreen240_StatusLine(void)
{
    // Called from the key handler, which runs before the next Update(), so the
    // selected zoom and the live scale genuinely differ for a few frames here.
    // Label them rather than printing two numbers that look contradictory.
    snprintf(sStatus, sizeof(sStatus),
             "enabled=%d selected=%d%% (currently %d%%, src %dx%d, margins %d/%d) textbox=%d",
             (int)sEnabled, sZoomSteps[sZoomIndex], sScale,
             gRenderWidth, gRenderHeight,
             gRenderTopMargin, gRenderBottomMargin, (int)sShiftTextBox);
    return sStatus;
}

// Only the field. Battles, menus, the title screen and every cutscene are laid
// out for a 240x160 screen and their BG maps hold nothing useful outside it, so
// they keep the stock frame and the companion panel.
static bool32 IsFieldView(void)
{
    if (gMain.callback2 != CB2_Overworld)
        return FALSE;
    if (gMain.inBattle)
        return FALSE;
    if (gMapHeader.mapLayout == NULL)
        return FALSE;
    // The field runs BG mode 0. Anything else here means a field effect has
    // reconfigured the PPU and the margins would show whatever is left in VRAM.
    if ((REG_DISPCNT & 7) != 0)
        return FALSE;
    return TRUE;
}

// Zooming crops the sides, and every piece of field UI lives there: the
// dialogue box is 232px wide and the start menu sits hard against the right
// edge. Emerald's messages carry their line breaks in the strings, so the box
// cannot simply be made narrower -- instead the view pulls back to 1:1 while
// any of it is on screen, where all of it fits, and zooms back in afterwards.
static bool32 FieldUiOpen(void)
{
    if (!IsFieldMessageBoxHidden())                     // dialogue, signposts
        return TRUE;
    if (GetStartMenuWindowId() != WINDOW_NONE)          // start menu
        return TRUE;
    if (ArePlayerFieldControlsLocked())                 // scripts, and the
        return TRUE;                                    // start menu's lock
    return FALSE;
}

// NOT gMenuCallback. It is set to HandleStartMenuInput when the start menu
// opens and never cleared when it closes -- the task is destroyed but the
// pointer keeps its stale value for the rest of the process. Testing it
// latched the view at 1:1 from the first press of START onwards, with no way
// back short of restarting the game. Both signals above clear themselves:
// RemoveStartMenuWindow() resets the window id, and HideStartMenuWindow()
// calls UnlockPlayerFieldControls().

// Source region for a given scale, rounded to even so the crop/margin either
// side of centre is a whole number of pixels.
static int SourceSizeForScale(int scale)
{
    int size = (DISPLAY_WIDTH * 100 + scale / 2) / scale;

    if (size & 1)
        size++;
    if (size > MAX_RENDER_HEIGHT)
        size = MAX_RENDER_HEIGHT;
    return size;
}

void Fullscreen240_Update(void)
{
    int target;
    int source;

    sActive = sEnabled && IsFieldView();
    if (!sActive)
    {
        // Stock geometry: the 240x160 GBA frame with the companion panel below.
        gRenderWidth = DISPLAY_WIDTH;
        gRenderMargin = 0;
        gRenderTopMargin = 0;
        gRenderBottomMargin = 0;
        gRenderHeight = DISPLAY_HEIGHT;
        sScale = sZoomSteps[sZoomIndex];
        return;
    }

    target = FieldUiOpen() ? SCALE_ONE_TO_ONE : sZoomSteps[sZoomIndex];
    if (sScale < target)
        sScale = (sScale + SCALE_STEP > target) ? target : sScale + SCALE_STEP;
    else if (sScale > target)
        sScale = (sScale - SCALE_STEP < target) ? target : sScale - SCALE_STEP;

    source = SourceSizeForScale(sScale);

    // A negative margin is a crop: buffer column 0 holds game column
    // -gRenderMargin, so -40 means the frame starts at game column 40. The
    // renderer works in game-space coordinates throughout, so cropping and
    // widening are the same arithmetic with opposite signs.
    gRenderWidth = source;
    gRenderMargin = -(DISPLAY_WIDTH - source) / 2;
    gRenderHeight = source;
    gRenderTopMargin = (source - DISPLAY_HEIGHT) / 2;
    gRenderBottomMargin = source - DISPLAY_HEIGHT - gRenderTopMargin;
    if (sScale == SCALE_ONE_TO_ONE)
    {
        // At 1:1 the margins are capped by the map buffer, not by the panel.
        gRenderTopMargin = sMargin;
        gRenderBottomMargin = sMargin;
        gRenderHeight = DISPLAY_HEIGHT + 2 * sMargin;
    }
    if (sTopSplit >= 0 && gRenderHeight > DISPLAY_HEIGHT
     && sTopSplit <= gRenderHeight - DISPLAY_HEIGHT)
    {
        gRenderTopMargin = sTopSplit;
        gRenderBottomMargin = gRenderHeight - DISPLAY_HEIGHT - sTopSplit;
    }
}

bool32 Fullscreen240_Active(void)
{
    return sActive;
}

// Rows the rendered frame occupies on the 240-row panel. Equal to gRenderHeight
// at 1:1 (so a 208-row view is letterboxed, never stretched) and twice it at
// 2x. Anything left over is painted black by the compositor.
int Fullscreen240_DestHeight(void)
{
    int height = gRenderHeight * sScale / 100;

    return height > MAX_RENDER_HEIGHT ? MAX_RENDER_HEIGHT : height;
}

// Read from the game thread when the standard text box windows are created, so
// it must not depend on anything the platform thread is mid-way through
// changing. Both flags are set by whole-word stores, and the worst a torn read
// could do is place the box one screenful wrong until the next map load.
//
// Not gated on the current zoom: the view is always back at 1:1 by the time a
// box is drawn (see FieldUiOpen), and the box's tilemap position is fixed at
// map load, long before any of that.
int Fullscreen240_TextBoxShiftRows(void)
{
    if (!sEnabled || !sShiftTextBox)
        return 0;
    // Follow the configured bottom margin, not a constant: the box has to land
    // on the bottom edge of whatever the 1:1 view turns out to be, and that is
    // capped by the map buffer. Whole tile rows only.
    return sMargin / 8;
}

#endif // RG_NANO_FULLSCREEN
