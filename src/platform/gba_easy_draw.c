#ifdef RENDERER_EASY_DRAW
#include "global.h"
#include <stdbool.h>
#include "platform.h"
#include "platform/dma.h"

#define mosaicBGEffectX (REG_MOSAIC & 0xF)
#define mosaicBGEffectY ((REG_MOSAIC >> 4) & 0xF)
#define mosaicSpriteEffectX ((REG_MOSAIC >> 8) & 0xF)
#define mosaicSpriteEffectY ((REG_MOSAIC >> 12) & 0xF)
#define applyBGHorizontalMosaicEffect(x) (x - (x % (mosaicBGEffectX+1)))
#define applyBGVerticalMosaicEffect(y) (y - (y % (mosaicBGEffectY+1)))
#define applySpriteHorizontalMosaicEffect(x) (x - (x % (mosaicSpriteEffectX+1)))
#define applySpriteVerticalMosaicEffect(y) (y - (y % (mosaicSpriteEffectY+1)))

#define getAlphaBit(x) ((x >> 15) & 1)
#define getRedChannel(x) ((x >>  0) & 0x1F)
#define getGreenChannel(x) ((x >>  5) & 0x1F)
#define getBlueChannel(x) ((x >>  10) & 0x1F)
#define isbgEnabled(x) ((REG_DISPCNT >> 8) & 0xF) & (1 << x)

#define WINMASK_BG0    (1 << 0)
#define WINMASK_BG1    (1 << 1)
#define WINMASK_BG2    (1 << 2)
#define WINMASK_BG3    (1 << 3)
#define WINMASK_OBJ    (1 << 4)
#define WINMASK_CLR    (1 << 5)
#define WINMASK_WINOUT  (1 << 6)

extern void (*const gIntrTable[])(void);

int gRenderWidth = DISPLAY_WIDTH;
int gRenderMargin = 0;

#if RG_NANO_FULLSCREEN
// See include/platform.h. Set once per frame from the platform layer while the
// game thread is parked, never mid-frame.
int gRenderHeight = DISPLAY_HEIGHT;
int gRenderTopMargin = 0;
int gRenderBottomMargin = 0;

// While set, BG0 is left out of the world pass and drawn separately at 1:1 by
// DrawUiOverlay. Same lifetime rule as the geometry globals.
bool gUiOverlayActive;
#endif

struct scanlineData {
    uint16_t layers[4][MAX_RENDER_WIDTH];
    uint16_t spriteLayers[4][MAX_RENDER_WIDTH];
    uint16_t bgcnts[4];
    uint16_t winMask[MAX_RENDER_WIDTH];
    // Set when a sprite pixel is actually written at that priority, so the
    // compositor can skip the priorities that have nothing on this scanline.
    bool spriteAtPriority[4];
    //priority bookkeeping
    char bgtoprio[4]; //background to priority
    char prioritySortedBgs[4][4];
    char prioritySortedBgsCount[4];
};

struct bgPriority {
    char priority;
    char subPriority;
};

static const uint16_t bgMapSizes[][2] =
{
    {32, 32},
    {64, 32},
    {32, 64},
    {64, 64},
};

// `line` is restrict-qualified deliberately. The build uses -fno-strict-aliasing,
// so without it the compiler must assume each store into the layer buffer might
// have modified VRAM or the palette, and reloads the tile bytes and palette
// entries on every pixel. The layer buffer is a scanline-local array that never
// overlaps either, and telling the compiler so is worth ~2x in this loop.
static void RenderBGScanline(int bgNum, uint16_t control, uint16_t hoffs, uint16_t voffs, int lineNum, uint16_t *restrict line)
{
    unsigned int charBaseBlock = (control >> 2) & 3;
    unsigned int screenBaseBlock = (control >> 8) & 0x1F;
    unsigned int bitsPerPixel = ((control >> 7) & 1) ? 8 : 4;
    unsigned int mapWidth = bgMapSizes[control >> 14][0];
    unsigned int mapHeight = bgMapSizes[control >> 14][1];
    unsigned int mapWidthInPixels = mapWidth * 8;
    unsigned int mapHeightInPixels = mapHeight * 8;

    uint8_t *bgtiles = (uint8_t *)BG_CHAR_ADDR(charBaseBlock);
    uint16_t *pal = (uint16_t *)PLTT;
     
    if (control & BGCNT_MOSAIC)
        lineNum = applyBGVerticalMosaicEffect(lineNum);

    hoffs &= 0x1FF;
    voffs &= 0x1FF;

    // Everything derived from the scanline's y coordinate is constant across
    // the whole row, but was being recomputed for every one of the 240 pixels
    // -- including the screen-base pointer itself. Hoisting it leaves only
    // genuinely per-pixel work in the loop below.
    uint16_t *bgmapRow = (uint16_t *)BG_SCREEN_ADDR(screenBaseBlock);
    unsigned int yy = (lineNum + voffs) & 0x1FF;
    if (yy > 255 && mapHeightInPixels > 256) {
        //the width check is for 512x512 mode support, it jumps by two screen bases instead
        bgmapRow += (mapWidthInPixels > 256) ? 0x800 : 0x400;
    }
    yy &= 0xFF;
    unsigned int mapRowOffset = (yy / 8) * 32;
    unsigned int tileYBase = yy % 8;

    // Fast path. Without mosaic, xx advances in lockstep with the loop counter,
    // so a run of up to 8 pixels shares one tilemap entry. The map lookup, flip
    // decode, tile address and palette base were all being redone for each of
    // the 240 pixels; doing them once per span is where the time goes. Both the
    // 0x1FF wrap and the 256px screen-base step land on tile boundaries, so
    // nothing inside a span can cross either. Mosaic breaks the monotonicity of
    // xx, so it keeps the original per-pixel loop below.
    if (!(control & BGCNT_MOSAIC))
    {
        // Loop-invariant: whether the margin columns need clipping at all.
        const bool clipMargins = (mapWidthInPixels < (unsigned int)gRenderWidth);
        int i = 0;

        while (i < gRenderWidth)
        {
            int x = i - gRenderMargin;
            unsigned int xx = (x + hoffs) & 0x1FF;
            uint16_t *bgmap = bgmapRow;
            unsigned int tileX0;
            unsigned int tileY;
            uint16_t entry;
            const uint8_t *tileRow;
            const uint16_t *palRow;
            bool flipX;
            int span;
            int k;

            if (xx > 255 && mapWidthInPixels > 256)
                bgmap += 0x400;
            xx &= 0xFF;

            tileX0 = xx & 7;
            span = 8 - (int)tileX0;
            if (span > gRenderWidth - i)
                span = gRenderWidth - i;

            entry = bgmap[mapRowOffset + (xx >> 3)];
            tileY = tileYBase;
            if (entry & (1 << 11))
                tileY = 7 - tileY;
            flipX = (entry & (1 << 10)) != 0;
            tileRow = bgtiles + (entry & 0x3FF) * (bitsPerPixel * 8) + tileY * bitsPerPixel;
            palRow = (bitsPerPixel == 4) ? pal + 16 * ((entry >> 12) & 0xF) : pal;

            // Hot case: 4bpp, unflipped, no margin clipping. Two 4bpp pixels
            // share one byte, so stepping them in pairs halves the tile-data
            // loads and drops the per-pixel odd/even branch. This is where
            // dense scenes spend their time -- there every pixel is opaque and
            // actually stored, which is why they cost 2.2x a sparse one.
            if (bitsPerPixel == 4 && !flipX && !clipMargins && span == 8 && tileX0 == 0)
            {
                // Whole-tile case, which is every span but the first on a
                // scanline. tileRow is always 4-byte aligned (tiles are 32
                // bytes, rows 4), so the eight 4bpp pixels are one 32-bit load
                // and eight nibble extracts -- no per-pixel reload hazard.
                uint32_t row = *(const uint32_t *)tileRow;
                uint16_t *restrict out = line + i;

                // A wholly transparent tile row writes nothing, so the eight
                // nibble extracts and eight branches below are pure waste --
                // and they are the common case, not a corner one. BG0 carries
                // only text and is blank across almost every field scanline,
                // and the overworld's upper map layers are mostly blank too.
                // One compare against a value already loaded skips all of it.
                if (row != 0)
                {
                    for (k = 0; k < 8; k++)
                    {
                        unsigned int pixel = (row >> (k * 4)) & 0xF;
                        if (pixel != 0)
                            out[k] = palRow[pixel] | 0x8000;
                    }
                }
            }
            else if (bitsPerPixel == 4 && !flipX && !clipMargins)
            {
                unsigned int tileX = tileX0;
                uint16_t *restrict out = line + i;

                k = 0;
                if ((tileX & 1) && k < span)
                {
                    uint8_t pixel = tileRow[tileX >> 1] >> 4;
                    if (pixel != 0)
                        out[k] = palRow[pixel] | 0x8000;
                    k++;
                    tileX++;
                }
                for (; k + 1 < span; k += 2, tileX += 2)
                {
                    uint8_t byte = tileRow[tileX >> 1];
                    uint8_t low = byte & 0xF;
                    uint8_t high = byte >> 4;

                    if (low != 0)
                        out[k] = palRow[low] | 0x8000;
                    if (high != 0)
                        out[k + 1] = palRow[high] | 0x8000;
                }
                if (k < span)
                {
                    uint8_t byte = tileRow[tileX >> 1];
                    uint8_t pixel = (tileX & 1) ? (byte >> 4) : (byte & 0xF);
                    if (pixel != 0)
                        out[k] = palRow[pixel] | 0x8000;
                }
            }
            else
            for (k = 0; k < span; k++)
            {
                int xi = i + k;
                unsigned int tileX = tileX0 + (unsigned int)k;
                uint8_t pixel;

                // Same margin rule as the per-pixel path below.
                if (clipMargins)
                {
                    int xg = xi - gRenderMargin;
                    if (xg < 0 || xg >= DISPLAY_WIDTH)
                        continue;
                }

                if (flipX)
                    tileX = 7 - tileX;

                if (bitsPerPixel == 4)
                {
                    pixel = tileRow[tileX >> 1];
                    if (tileX & 1)
                        pixel >>= 4;
                    else
                        pixel &= 0xF;

                    if (pixel != 0)
                        line[xi] = palRow[pixel] | 0x8000;
                }
                else
                {
                    line[xi] = palRow[tileRow[tileX]] | 0x8000;
                }
            }

            i += span;
        }
        return;
    }

    for (int i = 0; i < gRenderWidth; i++)
    {
        // Buffer index 0 holds game-space column -gRenderMargin. The 0x1FF
        // mask below wraps the negative columns into the BG map the same way
        // the hardware does, so a 512px-wide map fills the margins with real
        // tiles and a 256px one simply repeats -- which is why only screens
        // using wide maps let the margins show (see gRenderMarginsVisible).
        int x = i - gRenderMargin;

        // A map narrower than the widened frame has nothing to show out here --
        // it would just repeat its left edge -- so leave the margin columns to
        // whichever layer is wide enough to fill them, or to the backdrop.
        // This is what keeps menus, battles and the overworld's own text layer
        // from smearing across the widened frame. Note the comparison is
        // against gRenderWidth, not 240: a 256px map genuinely does have
        // content for an 8px margin, and only comes up short past that.
        if (mapWidthInPixels < gRenderWidth && (x < 0 || x >= DISPLAY_WIDTH))
            continue;

        // adjust for scroll
        unsigned int xx;
        if (control & BGCNT_MOSAIC)
            xx = (applyBGHorizontalMosaicEffect(x) + hoffs) & 0x1FF;
        else
            xx = (x + hoffs) & 0x1FF;

        uint16_t *bgmap = bgmapRow;

        //if x goes above 255 pixels it goes to the next screen base which are 0x400 WORDs long
        if (xx > 255 && mapWidthInPixels > 256) {
            bgmap += 0x400;
        }

        //maximum width for bgtile block is 256
        xx &= 0xFF;

        uint16_t entry = bgmap[mapRowOffset + xx / 8];

        unsigned int tileNum = entry & 0x3FF;
        unsigned int paletteNum = (entry >> 12) & 0xF;

        unsigned int tileX = xx % 8;
        unsigned int tileY = tileYBase;

        // Flip if necessary
        if (entry & (1 << 10))
            tileX = 7 - tileX;
        if (entry & (1 << 11))
            tileY = 7 - tileY;

        uint16_t tileLoc = tileNum * (bitsPerPixel * 8);
        uint16_t tileLocY = tileY * bitsPerPixel;
        uint16_t tileLocX = tileX;
        if (bitsPerPixel == 4)
            tileLocX /= 2;

        uint8_t pixel = bgtiles[tileLoc + tileLocY + tileLocX];

        if (bitsPerPixel == 4) {
            if (tileX & 1)
                pixel >>= 4;
            else
                pixel &= 0xF;

            if (pixel != 0)
                line[i] = pal[16 * paletteNum + pixel] | 0x8000;
        }
        else {
            line[i] = pal[pixel] | 0x8000;
        }
    }
}

static inline uint32_t getBgX(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2X;
    }
    else if (bgNumber == 3)
    {
        return REG_BG3X;
    }
}

static inline uint32_t getBgY(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2Y;
    }
    else if (bgNumber == 3)
    {
        return REG_BG3Y;
    }
}

static inline uint16_t getBgPA(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2PA;
    }
    else if (bgNumber == 3)
    {
        return REG_BG3PA;
    }
}

static inline uint16_t getBgPB(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2PB;
    }
    else if (bgNumber == 3)
    {
        return REG_BG3PB;
    }
}

static inline uint16_t getBgPC(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2PC;
    }
    else if (bgNumber == 3)
    {
        return REG_BG3PC;
    }
}

static inline uint16_t getBgPD(int bgNumber)
{
    if (bgNumber == 2)
    {
        return REG_BG2PD;
    }
    else if (bgNumber == 3)
    {
        return REG_BG3PD;
    }
}

static void RenderRotScaleBGScanline(int bgNum, uint16_t control, uint16_t x, uint16_t y, int lineNum, uint16_t *line)
{
    vBgCnt *bgcnt = (vBgCnt *)&control;
    unsigned int charBaseBlock = bgcnt->charBaseBlock;
    unsigned int screenBaseBlock = bgcnt->screenBaseBlock;
    unsigned int mapWidth = 1 << (4 + (bgcnt->screenSize)); // number of tiles

    uint8_t *bgtiles = (uint8_t *)(VRAM_ + charBaseBlock * 0x4000);
    uint8_t *bgmap = (uint8_t *)(VRAM_ + screenBaseBlock * 0x800);
    uint16_t *pal = (uint16_t *)PLTT;

    if (control & BGCNT_MOSAIC)
        lineNum = applyBGVerticalMosaicEffect(lineNum);
    

    s16 pa = getBgPA(bgNum);
    s16 pb = getBgPB(bgNum);
    s16 pc = getBgPC(bgNum);
    s16 pd = getBgPD(bgNum);

    int sizeX = 128;
    int sizeY = 128;

    switch (bgcnt->screenSize)
    {
    case 0:
        break;
    case 1:
        sizeX = sizeY = 256;
        break;
    case 2:
        sizeX = sizeY = 512;
        break;
    case 3:
        sizeX = sizeY = 1024;
        break;
    }

    int maskX = sizeX - 1;
    int maskY = sizeY - 1;

    int yshift = ((control >> 14) & 3) + 4;

    /*int dx = pa & 0x7FFF;
    if (pa & 0x8000)
        dx |= 0xFFFF8000;
    int dmx = pb & 0x7FFF;
    if (pb & 0x8000)
        dmx |= 0xFFFF8000;
    int dy = pc & 0x7FFF;
    if (pc & 0x8000)
        dy |= 0xFFFF8000;
    int dmy = pd & 0x7FFF;
    if (pd & 0x8000)
        dmy |= 0xFFFF8000;*/

    s32 currentX = getBgX(bgNum);
    s32 currentY = getBgY(bgNum);
    //sign extend 28 bit number
    currentX = ((currentX & (1 << 27)) ? currentX | 0xF0000000 : currentX);
    currentY = ((currentY & (1 << 27)) ? currentY | 0xF0000000 : currentY);

    currentX += lineNum * pb;
    currentY += lineNum * pd;

    int realX = currentX;
    int realY = currentY;

    // The affine walk steps one texel per column, so rewind it to the first
    // rendered column: gRenderMargin columns left of the GBA viewport.
    realX -= gRenderMargin * pa;
    realY -= gRenderMargin * pc;

    if (bgcnt->areaOverflowMode)
    {
        for (int x = 0; x < gRenderWidth; x++)
        {
            int xxx = (realX >> 8) & maskX;
            int yyy = (realY >> 8) & maskY;

            int tile = bgmap[(xxx >> 3) + ((yyy >> 3) << yshift)];

            int tileX = xxx & 7;
            int tileY = yyy & 7;

            uint8_t pixel = bgtiles[(tile << 6) + (tileY << 3) + tileX];

            if (pixel != 0) {
                line[x] = pal[pixel] | 0x8000;
            }

            realX += pa;
            realY += pc;
        }
    }
    else
    {
        for (int x = 0; x < gRenderWidth; x++)
        {
            int xxx = (realX >> 8);
            int yyy = (realY >> 8);

            if (xxx < 0 || yyy < 0 || xxx >= sizeX || yyy >= sizeY)
            {
                //line[x] = 0x80000000;
            }
            else
            {
                int tile = bgmap[(xxx >> 3) + ((yyy >> 3) << yshift)];

                int tileX = xxx & 7;
                int tileY = yyy & 7;

                uint8_t pixel = bgtiles[(tile << 6) + (tileY << 3) + tileX];

                if (pixel != 0) {
                    line[x] = pal[pixel] | 0x8000;
                }
            }
            realX += pa;
            realY += pc;
        }
    }
    //the only way i could figure out how to get accurate mosaic on affine bgs 
    //luckily i dont think pokemon emerald uses mosaic on affine bgs
    if (control & BGCNT_MOSAIC && mosaicBGEffectX > 0)
    {
        for (int i = 0; i < gRenderWidth; i++)
        {
            // Mosaic snaps to a grid anchored on the GBA viewport, so it has
            // to be quantised in game space and shifted back into the buffer.
            int x = i - gRenderMargin;
            uint16_t color = line[applyBGHorizontalMosaicEffect(x) + gRenderMargin];
            line[i] = color;

        }
    }
}

const u8 spriteSizes[][2] =
{
    {8, 16},
    {8, 32},
    {16, 32},
    {32, 64},
};

static uint16_t alphaBlendColor(uint16_t targetA, uint16_t targetB)
{
    unsigned int eva = REG_BLDALPHA & 0x1F;
    unsigned int evb = (REG_BLDALPHA >> 8) & 0x1F;
    // shift right by 4 = division by 16
    unsigned int r = ((getRedChannel(targetA) * eva) + (getRedChannel(targetB) * evb)) >> 4;
    unsigned int g = ((getGreenChannel(targetA) * eva) + (getGreenChannel(targetB) * evb)) >> 4;
    unsigned int b = ((getBlueChannel(targetA) * eva) + (getBlueChannel(targetB) * evb)) >> 4;
    
    if (r > 31)
        r = 31;
    if (g > 31)
        g = 31;
    if (b > 31)
        b = 31;

     return r | (g << 5) | (b << 10) | (1 << 15);
}

static uint16_t alphaBrightnessIncrease(uint16_t targetA)
{
    unsigned int evy = (REG_BLDY & 0x1F);
    unsigned int r = getRedChannel(targetA) + (31 - getRedChannel(targetA)) * evy / 16;
    unsigned int g = getGreenChannel(targetA) + (31 - getGreenChannel(targetA)) * evy / 16;
    unsigned int b = getBlueChannel(targetA) + (31 - getBlueChannel(targetA)) * evy / 16;
    
    if (r > 31)
        r = 31;
    if (g > 31)
        g = 31;
    if (b > 31)
        b = 31;
    
     return r | (g << 5) | (b << 10) | (1 << 15);
}

static uint16_t alphaBrightnessDecrease(uint16_t targetA)
{
    unsigned int evy = (REG_BLDY & 0x1F);
    unsigned int r = getRedChannel(targetA) - getRedChannel(targetA) * evy / 16;
    unsigned int g = getGreenChannel(targetA) - getGreenChannel(targetA) * evy / 16;
    unsigned int b = getBlueChannel(targetA) - getBlueChannel(targetA) * evy / 16;
    
    if (r > 31)
        r = 31;
    if (g > 31)
        g = 31;
    if (b > 31)
        b = 31;
    
     return r | (g << 5) | (b << 10) | (1 << 15);
}

//outputs the blended pixel in colorOutput, the prxxx are the bg priority and subpriority, pixelpos is pixel offset in scanline
static bool alphaBlendSelectTargetB(struct scanlineData* scanline, uint16_t* colorOutput, char prnum, char prsub, int pixelpos, bool spriteBlendEnabled)
{   
    //iterate trough every possible bg to blend with, starting from specified priorities from arguments
    for (unsigned int blndprnum = prnum; blndprnum <= 3; blndprnum++)
    {
        //check if sprite is available to blend with, if sprite blending is enabled
        // spriteAtPriority must be checked first: rows with no sprites on this
        // scanline are no longer cleared, so their contents are stale.
        if (spriteBlendEnabled == true && scanline->spriteAtPriority[blndprnum]
         && getAlphaBit(scanline->spriteLayers[blndprnum][pixelpos]) == 1)
        {
            *colorOutput = scanline->spriteLayers[blndprnum][pixelpos];
            return true;
        }
            
        for (unsigned int blndprsub = prsub; blndprsub < scanline->prioritySortedBgsCount[blndprnum]; blndprsub++)
        {
            char currLayer = scanline->prioritySortedBgs[blndprnum][blndprsub];
            if (getAlphaBit( scanline->layers[currLayer][pixelpos] ) == 1 && REG_BLDCNT & ( 1 << (8 + currLayer)) && isbgEnabled(currLayer))
            {
                *colorOutput = scanline->layers[currLayer][pixelpos];
                return true;
            }
            //if we hit a non target layer we should bail
            if ( getAlphaBit( scanline->layers[currLayer][pixelpos] ) == 1 && isbgEnabled(currLayer) && prnum != blndprnum )
            {
                return false;
            }
        }
        prsub = 0; //start from zero in the next iteration
    }
    //no background got hit, check if backdrop is enabled and return it if enabled otherwise fail
    if (REG_BLDCNT & BLDCNT_TGT2_BD)
    {
        *colorOutput = *(uint16_t*)PLTT;
        return true;
    }
    else
    {
        return false;
    }
}

//checks if window horizontal is in bounds and takes account WIN wraparound
// Window registers can only express coordinates inside the GBA's 240px
// screen, so a game that wants a full-screen window (the overworld does, via
// WIN0H = 0x00FF) can never say "cover the widescreen margins too". Treat an
// edge sitting on the screen boundary as "to the edge of the frame" and
// stretch it across the margin; interior edges (the cave flash circle, battle
// transitions) keep their exact game-space coordinates.
// Only screens whose BG maps are wide enough to fill the margins get the
// extension; everywhere else (indoors, battles, menus) the window keeps its
// GBA bounds so the margins stay masked and sprites clip at the screen edge
// exactly like vanilla hardware. Set per scanline before the window mask.
static bool sWinExtendMargins;

static int winExtendLeft(u16 left)
{
    return (sWinExtendMargins && left == 0) ? -gRenderMargin : (int)left;
}

static int winExtendRight(u16 right)
{
    return (sWinExtendMargins && right >= DISPLAY_WIDTH) ? DISPLAY_WIDTH + gRenderMargin : (int)right;
}

#if RG_NANO_FULLSCREEN
// The vertical twin of winExtendLeft/Right. WIN0V = 0x00A0 is the overworld's
// way of saying "the whole screen"; without this the extra scanlines fall
// outside every window and get masked to WINOUT, which blanks the margins.
static int winExtendTop(u16 top)
{
    return (sWinExtendMargins && top == 0) ? -gRenderTopMargin : (int)top;
}

static int winExtendBottom(u16 bottom)
{
    return (sWinExtendMargins && bottom >= DISPLAY_HEIGHT)
         ? DISPLAY_HEIGHT + gRenderBottomMargin : (int)bottom;
}

static bool winCheckVerticalBounds(u16 top, u16 bottom, int vcount)
{
    if (top > bottom)
        return (vcount >= (int)top || vcount < (int)bottom);
    else
        return (vcount >= winExtendTop(top) && vcount < winExtendBottom(bottom));
}
#endif

static bool winCheckHorizontalBounds(u16 left, u16 right, int xpos)
{
    if (left > right)
        return (xpos >= (int)left || xpos < (int)right);
    else
        return (xpos >= winExtendLeft(left) && xpos < winExtendRight(right));
}

// Parts of this code heavily borrowed from NanoboyAdvance.
static void DrawSprites(struct scanlineData* scanline, int vcount, bool windowsEnabled)
{
    int i;
    unsigned int x;
    unsigned int y;
    void *objtiles = VRAM_ + 0x10000;
    unsigned int blendMode = (REG_BLDCNT >> 6) & 3;
    bool winShouldBlendPixel = true;

    int16_t matrix[2][2] = {};

    if (!(REG_DISPCNT & (1 << 6)))
    {
        puts("2-D OBJ Character mapping not supported.");
    }

    for (i = 127; i >= 0; i--)
    {
        struct OamData *oam = &((struct OamData *)OAM)[i];
        unsigned int width;
        unsigned int height;
        uint16_t *pixels;

        bool isAffine  = oam->affineMode & 1;
        bool doubleSizeOrDisabled = (oam->affineMode >> 1) & 1;
        bool isSemiTransparent = (oam->objMode == 1);
        bool isObjWin = (oam->objMode == 2);

        if (!(isAffine) && doubleSizeOrDisabled) // disable for non-affine
        {
            continue;
        }

        if (oam->shape == 0)
        {
            width = (1 << oam->size) * 8;
            height = (1 << oam->size) * 8;
        }
        else if (oam->shape == 1) // wide
        {
            width = spriteSizes[oam->size][1];
            height = spriteSizes[oam->size][0];
        }
        else if (oam->shape == 2) // tall
        {
            width = spriteSizes[oam->size][0];
            height = spriteSizes[oam->size][1];
        }
        else
        {
            continue; // prohibited, do not draw
        }

        int rect_width = width;
        int rect_height = height;

        int half_width = width / 2;
        int half_height = height / 2;

        pixels = scanline->spriteLayers[oam->priority];

        int32_t x = oam->x;
        int32_t y = oam->y;

        // OAM x is 9 bits, so position wraps within 512. The old cutoff
        // treated everything at 240+ as wrapping in from the left, which on
        // real hardware is indistinguishable from offscreen-right -- but with
        // widened margins, x in [240, 240+margin) is genuinely visible on the
        // right. Only wrap values too large to reach the frame from the right;
        // [384, 511] still decodes to [-128, -1] for sprites entering from
        // the left (max sprite width is 64, margin at most 24).
        if (x >= 512 - 128)
            x -= 512;
#if RG_NANO_FULLSCREEN
        // Same reasoning as the x wrap above, applied vertically: with rows
        // rendered below the viewport, y in [160, 192) is genuinely on screen
        // rather than a sprite entering from the top, so only wrap values too
        // large to reach the frame from below (max sprite height is 64).
        if (y >= 256 - 64)
            y -= 256;
#else
        if (y >= DISPLAY_HEIGHT)
            y -= 256;
#endif

        if (isAffine)
        {
            //TODO: there is probably a better way to do this
            u8 matrixNum = oam->matrixNum * 4;

            struct OamData *oam1 = &((struct OamData *)OAM)[matrixNum];
            struct OamData *oam2 = &((struct OamData *)OAM)[matrixNum + 1];
            struct OamData *oam3 = &((struct OamData *)OAM)[matrixNum + 2];
            struct OamData *oam4 = &((struct OamData *)OAM)[matrixNum + 3];

            matrix[0][0] = oam1->affineParam;
            matrix[0][1] = oam2->affineParam;
            matrix[1][0] = oam3->affineParam;
            matrix[1][1] = oam4->affineParam;

            if (doubleSizeOrDisabled) // double size for affine
            {
                rect_width *= 2;
                rect_height *= 2;
                half_width *= 2;
                half_height *= 2;
            }
        }
        else
        {
            // Identity
            matrix[0][0] = 0x100;
            matrix[0][1] = 0;
            matrix[1][0] = 0;
            matrix[1][1] = 0x100;
        }

        x += half_width;
        y += half_height;

        // Does this sprite actually draw on this scanline?
        if (vcount >= (y - half_height) && vcount < (y + half_height))
        {
            int local_y = (oam->mosaic == 1) ? applySpriteVerticalMosaicEffect(vcount) - y : vcount - y;
            int number  = oam->tileNum;
            int palette = oam->paletteNum;
            bool flipX  = !isAffine && ((oam->matrixNum >> 3) & 1);
            bool flipY  = !isAffine && ((oam->matrixNum >> 4) & 1);
            bool is8BPP  = oam->bpp & 1;

            for (int local_x = -half_width; local_x <= half_width; local_x++)
            {
                uint8_t *tiledata = (uint8_t *)objtiles;
                uint16_t *palette = (uint16_t *)(PLTT + 0x200);
                int local_mosaicX;
                int tex_x;
                int tex_y;

                // Game-space column, and where it lands in the render buffer.
                int global_x = local_x + x;
                int buf_x = global_x + gRenderMargin;

                if (buf_x < 0 || buf_x >= gRenderWidth)
                    continue;

                if (oam->mosaic == 1)
                {
                    //mosaic effect has to be applied to global coordinates otherwise the mosaic will scroll
                    local_mosaicX = applySpriteHorizontalMosaicEffect(global_x) - x;
                    tex_x = ((matrix[0][0] * local_mosaicX + matrix[0][1] * local_y) >> 8) + (width / 2);
                    tex_y = ((matrix[1][0] * local_mosaicX + matrix[1][1] * local_y) >> 8) + (height / 2);
                }else{
                    tex_x = ((matrix[0][0] * local_x + matrix[0][1] * local_y) >> 8) + (width / 2);
                    tex_y = ((matrix[1][0] * local_x + matrix[1][1] * local_y) >> 8) + (height / 2);
                }

                /* Check if transformed coordinates are inside bounds. */

                if (tex_x >= width || tex_y >= height || tex_x < 0 || tex_y < 0)
                    continue;

                if (flipX)
                    tex_x = width  - tex_x - 1;
                if (flipY)
                    tex_y = height - tex_y - 1;

                int tile_x = tex_x % 8;
                int tile_y = tex_y % 8;
                int block_x = tex_x / 8;
                int block_y = tex_y / 8;
                int block_offset = ((block_y * (REG_DISPCNT & 0x40 ? (width / 8) : 16)) + block_x);
                uint16_t pixel = 0;

                if (!is8BPP)
                {
                    pixel = tiledata[(block_offset + oam->tileNum) * 32 + (tile_y * 4) + (tile_x / 2)];
                    if (tile_x & 1)
                        pixel >>= 4;
                    else
                        pixel &= 0xF;
                    palette += oam->paletteNum * 16;
                }
                else
                {
                    pixel = tiledata[(block_offset * 2 + oam->tileNum) * 32 + (tile_y * 8) + tile_x];
                }

                if (pixel != 0)
                {
                    uint16_t color = palette[pixel];;
                    
                    //if sprite mode is 2 then write to the window mask instead
                    if (isObjWin)
                    {
                        if (scanline->winMask[buf_x] & WINMASK_WINOUT)
                        scanline->winMask[buf_x] = (REG_WINOUT >> 8) & 0x3F;
                        continue;
                    }
                    //this code runs if pixel is to be drawn
                    {
                        //check if its enabled in the window (if window is enabled)
                        winShouldBlendPixel = (windowsEnabled == false || scanline->winMask[buf_x] & WINMASK_CLR);
                        
                        //has to be separated from the blend mode switch statement because of OBJ semi transparancy feature
                        if ((blendMode == 1 && REG_BLDCNT & BLDCNT_TGT1_OBJ && winShouldBlendPixel) || isSemiTransparent)
                        {
                            uint16_t targetA = color;
                            uint16_t targetB = 0;
                            if (alphaBlendSelectTargetB(scanline, &targetB, oam->priority, 0, buf_x, false))
                            {
                                color = alphaBlendColor(targetA, targetB);
                            }
                        }
                        else if (REG_BLDCNT & BLDCNT_TGT1_OBJ && winShouldBlendPixel)
                        {
                            switch (blendMode)
                            {
                            case 2:
                                color = alphaBrightnessIncrease(color);
                                break;
                            case 3:
                                color = alphaBrightnessDecrease(color);
                                break;
                            }
                        }
                        
                        //write pixel to pixel framebuffer
                        pixels[buf_x] = color | (1 << 15);
                        scanline->spriteAtPriority[oam->priority] = true;
                    }
                }
            }
        }
    }
}

#ifdef RG_NANO_PROFILE_PPU
#include <time.h>
int64_t gPpuProfileBg;
int64_t gPpuProfileClear;
int64_t gPpuProfileWin;
int64_t gPpuProfileObj;
int64_t gPpuProfileComposite;
unsigned int gPpuTextBgCalls;
unsigned int gPpuAffineBgCalls;
static int64_t PpuProfileNow(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
}
#define PPU_PROFILE_MARK(var) ((var) = PpuProfileNow())
#define PPU_PROFILE_ADD(acc, from, to) ((acc) += (to) - (from))
#define PPU_PROFILE_COUNT(counter) ((counter)++)
#else
#define PPU_PROFILE_MARK(var) ((void)0)
#define PPU_PROFILE_ADD(acc, from, to) ((void)0)
#define PPU_PROFILE_COUNT(counter) ((void)0)
#endif

// pixels is restrict for the same reason as RenderBGScanline's line: with
// -fno-strict-aliasing the compositor would otherwise reload the layer buffers
// and palette after every store into the output scanline.
static void DrawScanline(uint16_t *restrict pixels, int vcount)
{
#ifdef RG_NANO_PROFILE_PPU
    int64_t profileStart, profileClearEnd, profileBgEnd, profileWinEnd, profileObjEnd, profileEnd;
#endif
    unsigned int mode = REG_DISPCNT & 3;
    unsigned char numOfBgs = (mode == 0 ? 4 : 3);
    int bgnum, prnum;
    struct scanlineData scanline;
    unsigned int blendMode = (REG_BLDCNT >> 6) & 3;
    unsigned int xpos;


    PPU_PROFILE_MARK(profileStart);
    //initialize all priority bookkeeping data. Only the first gRenderWidth
    //columns are ever read, and the arrays are MAX_RENDER_WIDTH wide to leave
    //room for the widescreen margins, so clearing them in full every scanline
    //just burns memory bandwidth.
    {
        size_t usedRow = (size_t)gRenderWidth * sizeof(uint16_t);
        int layer;
        // winMask needs no clear: when windows are active every column is
        // written below, and when they are not it is never read. Clearing the
        // sprite rows lazily in DrawSprites was tried and measured worse -- it
        // moved the same blanking into the sprite path and cost more there.
        for (layer = 0; layer < 4; layer++)
        {
            memset(scanline.layers[layer], 0, usedRow);
            memset(scanline.spriteLayers[layer], 0, usedRow);
        }
        memset(scanline.spriteAtPriority, 0, sizeof(scanline.spriteAtPriority));
    }
    memset(scanline.prioritySortedBgsCount, 0, sizeof(scanline.prioritySortedBgsCount));
    PPU_PROFILE_MARK(profileClearEnd);

    for (bgnum = 0; bgnum < numOfBgs; bgnum++)
    {
        uint16_t bgcnt = *(uint16_t*)(REG_ADDR_BG0CNT + bgnum * 2);
        uint16_t priority;
        scanline.bgcnts[bgnum] = bgcnt;
        scanline.bgtoprio[bgnum] = priority = (bgcnt & 3);
        
        char priorityCount = scanline.prioritySortedBgsCount[priority];
        scanline.prioritySortedBgs[priority][priorityCount] = bgnum;
        scanline.prioritySortedBgsCount[priority]++;
    }
    
    switch (mode)
    {
    case 0:
        // All backgrounds are text mode
        for (bgnum = 3; bgnum >= 0; bgnum--)
        {
#if RG_NANO_FULLSCREEN
            // BG0 is the overworld's UI layer, and while the world is zoomed it
            // is drawn separately at 1:1 (DrawUiOverlay). Rendering it here too
            // would leave a magnified, side-cropped copy of the dialogue box
            // showing around the unscaled one. Leaving its layer buffer cleared
            // is enough: the compositor skips pixels with no alpha bit.
            if (gUiOverlayActive && bgnum == 0)
                continue;
#endif
            if (isbgEnabled(bgnum))
            {
                uint16_t bghoffs = *(uint16_t *)(REG_ADDR_BG0HOFS + bgnum * 4);
                uint16_t bgvoffs = *(uint16_t *)(REG_ADDR_BG0VOFS + bgnum * 4);
                
                PPU_PROFILE_COUNT(gPpuTextBgCalls);
                RenderBGScanline(bgnum, scanline.bgcnts[bgnum], bghoffs, bgvoffs, vcount, scanline.layers[bgnum]);
            }
        }
        
        break;
    case 1:
        // BG2 is affine
        bgnum = 2;
        if (isbgEnabled(bgnum))
        {
            PPU_PROFILE_COUNT(gPpuAffineBgCalls);
            RenderRotScaleBGScanline(bgnum, scanline.bgcnts[bgnum], REG_BG2X, REG_BG2Y, vcount, scanline.layers[bgnum]);
        }
        // BG0 and BG1 are text mode
        for (bgnum = 1; bgnum >= 0; bgnum--)
        {
            if (isbgEnabled(bgnum))
            {
                uint16_t bghoffs = *(uint16_t *)(REG_ADDR_BG0HOFS + bgnum * 4);
                uint16_t bgvoffs = *(uint16_t *)(REG_ADDR_BG0VOFS + bgnum * 4);
                
                PPU_PROFILE_COUNT(gPpuTextBgCalls);
                RenderBGScanline(bgnum, scanline.bgcnts[bgnum], bghoffs, bgvoffs, vcount, scanline.layers[bgnum]);
            }
        }
        break;
    default:
        DBGPRINTF("Video mode %u is unsupported.\n", mode);
        break;
    }
    PPU_PROFILE_MARK(profileBgEnd);

    bool windowsEnabled = false;
    uint16_t WIN0bottom, WIN0top, WIN0right, WIN0left;
    uint16_t WIN1bottom, WIN1top, WIN1right, WIN1left;
    bool WIN0enable, WIN1enable;
    WIN0enable = false;
    WIN1enable = false;

    // Extend screen-edge window bounds across the margins only when some
    // enabled text BG is 512px wide (screen size 1 or 3) and can actually
    // fill them -- the widened overworld. Otherwise keep GBA bounds so
    // narrow screens stay masked to the 240px view like real hardware.
    sWinExtendMargins = false;
    {
        int bgnum;
        for (bgnum = 0; bgnum < 4; bgnum++)
        {
            if ((REG_DISPCNT & (1 << (8 + bgnum)))
             && (scanline.bgcnts[bgnum] >> 14) & 1)
                sWinExtendMargins = true;
        }
    }

    //figure out if WIN0 masks on this scanline
    if (REG_DISPCNT & DISPCNT_WIN0_ON)
    {
        //acquire the window coordinates
        WIN0bottom = (REG_WIN0V & 0xFF); //y2;
        WIN0top = (REG_WIN0V & 0xFF00) >> 8; //y1;
        WIN0right = (REG_WIN0H & 0xFF); //x2
        WIN0left = (REG_WIN0H & 0xFF00) >> 8; //x1
        
        //figure out WIN Y wraparound and check bounds accordingly
#if RG_NANO_FULLSCREEN
        WIN0enable = winCheckVerticalBounds(WIN0top, WIN0bottom, vcount);
#else
        if (WIN0top > WIN0bottom) {
            if (vcount >= WIN0top || vcount < WIN0bottom)
                WIN0enable = true;
        } else {
            if (vcount >= WIN0top && vcount < WIN0bottom)
                WIN0enable = true;
        }
#endif
        
        windowsEnabled = true;
    }
    //figure out if WIN1 masks on this scanline
    if (REG_DISPCNT & DISPCNT_WIN1_ON)
    {
        // Read WIN1's own registers -- this used WIN0's, which made WIN1 a
        // silent duplicate of WIN0 instead of the empty window the overworld
        // configures (WIN1H = 0xFFFF).
        WIN1bottom = (REG_WIN1V & 0xFF); //y2;
        WIN1top = (REG_WIN1V & 0xFF00) >> 8; //y1;
        WIN1right = (REG_WIN1H & 0xFF); //x2
        WIN1left = (REG_WIN1H & 0xFF00) >> 8; //x1
        
#if RG_NANO_FULLSCREEN
        WIN1enable = winCheckVerticalBounds(WIN1top, WIN1bottom, vcount);
#else
        if (WIN1top > WIN1bottom) {
            if (vcount >= WIN1top || vcount < WIN1bottom)
                WIN1enable = true;
        } else {
            if (vcount >= WIN1top && vcount < WIN1bottom)
                WIN1enable = true;
        }
#endif
        
        windowsEnabled = true;
    }
    //enable windows if OBJwin is enabled
    if (REG_DISPCNT & DISPCNT_OBJWIN_ON && REG_DISPCNT & DISPCNT_OBJ_ON)
    {
        windowsEnabled = true;
    }
    
    //draw to pixel mask
    if (windowsEnabled)
    {
        // REG_WININ/REG_WINOUT are volatile I/O reads, so leaving them in the
        // loop body forced up to three reloads per pixel, 240 times a scanline,
        // for values that cannot change mid-scanline.
        const uint16_t maskWin0 = REG_WININ & 0x3F;
        const uint16_t maskWin1 = (REG_WININ >> 8) & 0x3F;
        const uint16_t maskOut = (REG_WINOUT & 0x3F) | WINMASK_WINOUT;

        for (xpos = 0; xpos < gRenderWidth; xpos++)
        {
            // Window bounds are register values in GBA screen space, so test
            // them against the game-space column rather than the buffer index.
            int gx = xpos - gRenderMargin;
            //win0 checks
            if (WIN0enable && winCheckHorizontalBounds(WIN0left, WIN0right, gx))
                scanline.winMask[xpos] = maskWin0;
            //win1 checks
            else if (WIN1enable && winCheckHorizontalBounds(WIN1left, WIN1right, gx))
                scanline.winMask[xpos] = maskWin1;
            else
                scanline.winMask[xpos] = maskOut;
        }
    }

    PPU_PROFILE_MARK(profileWinEnd);
    if (REG_DISPCNT & DISPCNT_OBJ_ON)
        DrawSprites(&scanline, vcount, windowsEnabled);
    PPU_PROFILE_MARK(profileObjEnd);

    //iterate trough every priority in order
    for (prnum = 3; prnum >= 0; prnum--)
    {
        for (int prsub = scanline.prioritySortedBgsCount[prnum] - 1; prsub >= 0; prsub--)
        {
            char bgnum = scanline.prioritySortedBgs[prnum][prsub];
            //if background is enabled then draw it
            if (isbgEnabled(bgnum))
            {
                uint16_t *src = scanline.layers[bgnum];
                //copy all pixels to framebuffer
                for (xpos = 0; xpos < gRenderWidth; xpos++)
                {
                    uint16_t color = src[xpos];
                    bool winEffectEnable = true;
                    
                    if (!getAlphaBit(color))
                        continue; //do nothing if alpha bit is not set
                    
                    if (windowsEnabled)
                    {
                        winEffectEnable = ((scanline.winMask[xpos] & WINMASK_CLR) >> 5);
                        //if bg is disabled inside the window then do not draw the pixel
                        if ( !(scanline.winMask[xpos] & 1 << bgnum) )
                            continue;
                    }
                    
                    //blending code
                    if (blendMode != 0 && REG_BLDCNT & (1 << bgnum) && winEffectEnable)
                    {
                        uint16_t targetA = color;
                        uint16_t targetB = 0;
                        char isSpriteBlendingEnabled;
                        
                        switch (blendMode)
                        {
                        case 1:
                            isSpriteBlendingEnabled = REG_BLDCNT & BLDCNT_TGT2_OBJ ? 1 : 0;
                            //find targetB and blend it
                            if (alphaBlendSelectTargetB(&scanline, &targetB, prnum, prsub+1, xpos, isSpriteBlendingEnabled))
                            {
                                color = alphaBlendColor(targetA, targetB);
                            }
                            break;
                        case 2:
                            color = alphaBrightnessIncrease(targetA);
                            break;
                        case 3:
                            color = alphaBrightnessDecrease(targetA);
                            break;
                        }
                    }
                    //write the pixel to scanline buffer output
                    pixels[xpos] = color;
                }
            }
        }
        //draw sprites on current priority
        uint16_t *src = scanline.spriteLayers[prnum];
        if (!scanline.spriteAtPriority[prnum])
            continue; // nothing drew here this scanline; skip 240 alpha tests
        for (xpos = 0; xpos < gRenderWidth; xpos++)
        {
            if (getAlphaBit(src[xpos]))
            {
                //check if sprite pixel draws inside window
                if (windowsEnabled && !(scanline.winMask[xpos] & WINMASK_OBJ))
                        continue;
                //draw the pixel
                pixels[xpos] = src[xpos];
            }
        }
    }
    PPU_PROFILE_MARK(profileEnd);
    PPU_PROFILE_ADD(gPpuProfileClear, profileStart, profileClearEnd);
    PPU_PROFILE_ADD(gPpuProfileBg, profileClearEnd, profileBgEnd);
    PPU_PROFILE_ADD(gPpuProfileWin, profileBgEnd, profileWinEnd);
    PPU_PROFILE_ADD(gPpuProfileObj, profileWinEnd, profileObjEnd);
    PPU_PROFILE_ADD(gPpuProfileComposite, profileObjEnd, profileEnd);
}

uint16_t *memsetu16(uint16_t *dst, uint16_t fill, size_t count)
{
    for (int i = 0; i < count; i++)
    {
        *dst++ = fill;
    }
}

// Diagnostic: how often the V-count interrupt (which drives m4aSoundVSync, and
// therefore all audio) actually fires. Reported on the [Audio] log line.
unsigned int gVCountIntrFires;

// When set, DrawFrame runs a full scanline pass for timing and interrupts but
// produces no pixels. Used to hold exact 60Hz game and audio timing on frames
// the CPU cannot also draw in budget.
bool gSkipPixelRender;

#if RG_NANO_FULLSCREEN
// Render BG0 on its own, at 1:1 and the full 240px width, into `out`.
//
// This is what lets the field UI stay unscaled over a zoomed world. BG0 is the
// overworld's dedicated UI layer -- sOverworldBgTemplates gives it its own char
// base and screenblock, away from the three map layers, and the field pins its
// scroll to 0 -- so splitting the frame along it is a clean seam rather than a
// hack. Everything the player reads (dialogue box, start menu, choice lists,
// the map name popup) is on it, and nothing else is.
//
// Pixels BG0 does not write are left 0. RenderBGScanline sets bit 15 on the
// ones it does, so the compositor gets its mask for free.
void DrawUiOverlay(uint16_t *out, int top, int rows)
{
    int savedWidth = gRenderWidth;
    int savedMargin = gRenderMargin;
    uint16_t control = REG_BG0CNT;
    uint16_t hoffs = REG_BG0HOFS;
    uint16_t voffs = REG_BG0VOFS;
    int i;

    // RenderBGScanline works in whatever the active render geometry is, which
    // while zoomed is a cropped 120px window. The overlay wants the whole
    // uncropped width -- that is the entire point of drawing it separately.
    // Safe to change here: the game thread is parked for all of
    // DrawComposedFrame, and both values are restored before it returns.
    gRenderWidth = DISPLAY_WIDTH;
    gRenderMargin = 0;
    for (i = 0; i < rows; i++)
    {
        uint16_t *line = out + i * DISPLAY_WIDTH;

        memsetu16(line, 0, DISPLAY_WIDTH);
        if (isbgEnabled(0))
            RenderBGScanline(0, control, hoffs, voffs, top + i, line);
    }
    gRenderWidth = savedWidth;
    gRenderMargin = savedMargin;
}
#endif

// One rendered row, backdrop first. `line` is a game-space scanline number and
// may sit outside [0, DISPLAY_HEIGHT) when vertical margins are active; buffer
// row 0 is game-space row -gRenderTopMargin.
static void DrawFrameScanline(uint16_t *pixels, int line)
{
    unsigned int blendMode = (REG_BLDCNT >> 6) & 3;
    uint16_t backdropColor = *(uint16_t *)PLTT;
    uint16_t *row;

    if (REG_BLDCNT & BLDCNT_TGT1_BD)
    {
        switch (blendMode)
        {
        case 2:
            backdropColor = alphaBrightnessIncrease(backdropColor);
            break;
        case 3:
            backdropColor = alphaBrightnessDecrease(backdropColor);
            break;
        }
    }

#if RG_NANO_FULLSCREEN
    // A negative margin crops: the outermost viewport rows then fall outside
    // the frame and must not be drawn at all, or they would write before the
    // start of the buffer.
    int bufferRow = line + gRenderTopMargin;

    if (bufferRow < 0 || bufferRow >= gRenderHeight)
        return;
    row = &pixels[bufferRow * gRenderWidth];
#else
    row = &pixels[(line + gRenderTopMargin) * gRenderWidth];
#endif
    memsetu16(row, backdropColor, gRenderWidth);
    DrawScanline(row, line);
}

void DrawFrame(uint16_t *pixels)
{
    int i;
    int j;
#if RG_NANO_FULLSCREEN
    // The margin rows sit outside the GBA's scanline timing, so they get no
    // REG_VCOUNT, no H-blank DMA and no H-blank interrupt of their own -- doing
    // any of that would change game timing, which is the one thing this must
    // not do. They are drawn from the register state that brackets the real
    // frame instead: the top rows before scanline 0, the bottom rows after
    // scanline 159. Anything driven per-scanline by an H-blank DMA therefore
    // reads flat across the margins.
    if (!gSkipPixelRender)
        for (i = -gRenderTopMargin; i < 0; i++)
            DrawFrameScanline(pixels, i);
#endif
    for (i = 0; i < DISPLAY_HEIGHT; i++)
    {
        REG_VCOUNT = i;
        if(((REG_DISPSTAT >> 8) & 0xFF) == REG_VCOUNT)
        {
            REG_DISPSTAT |= INTR_FLAG_VCOUNT;
#ifdef __ANDROID__
            if (REG_IE & INTR_FLAG_VCOUNT)
#else
            if (REG_DISPSTAT & DISPSTAT_VCOUNT_INTR)
#endif
            {
                    gVCountIntrFires++;
                    gIntrTable[0]();
            }
        }

        // Only the pixel production is optional. Everything else in this loop --
        // REG_VCOUNT, the V-count interrupt (which drives m4aSoundVSync and so
        // all audio), the H-blank DMAs and the H-blank interrupt -- runs exactly
        // as it always does, so a skipped frame is invisible to game timing.
        if (!gSkipPixelRender)
            DrawFrameScanline(pixels, i);
        
        REG_DISPSTAT |= INTR_FLAG_HBLANK;

        RunDMAs(DMA_HBLANK);
        
        if (REG_DISPSTAT & DISPSTAT_HBLANK_INTR)
            gIntrTable[3]();

        REG_DISPSTAT &= ~INTR_FLAG_HBLANK;
        REG_DISPSTAT &= ~INTR_FLAG_VCOUNT;
    }
#if RG_NANO_FULLSCREEN
    if (!gSkipPixelRender)
        for (i = DISPLAY_HEIGHT; i < DISPLAY_HEIGHT + gRenderBottomMargin; i++)
            DrawFrameScanline(pixels, i);
#endif
}
#endif
