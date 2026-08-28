#ifdef PLATFORM_RG_NANO

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <SDL.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include "global.h"
#include "cgb_audio.h"
#include "platform.h"
#include "rtc.h"
#include "gba/defines.h"
#include "gba/flash_internal.h"
#include "gba/io_reg.h"
#include "platform/dma.h"
#include "platform/dualscreen.h"
#include "platform/framedraw.h"
#include "mods/mod_manager.h"
#include "platform/rg_nano_asset_gate.h"
#include "platform/secondary_panel.h"
#include "experiments/fullscreen240/fullscreen240.h"

#define RG_NANO_DATA_DIR "/mnt/FunKey/.pokemon-emerald-nano"
// A GBA frame is 228 scanlines x 1232 cycles = 280896 cycles at 16.777216 MHz,
// i.e. 59.7275 Hz / 16.7427 ms -- not the 60.000 Hz / 16.6667 ms assumed here
// before. Pacing to 60 Hz held the game to a deadline 76us tighter than real
// hardware and made every frame count as late. This is the authentic period.
#define RG_NANO_FRAME_NS 16742706L
#define AUDIO_RING_SAMPLES 16384

extern void (*const gIntrTable[])(void);
extern void AgbMain(void);

static SDL_Surface *sScreen;
static SDL_Thread *sGameThread;
static SDL_sem *sFrameReady;
static SDL_sem *sVBlank;
static volatile sig_atomic_t sRunning = 1;
static uint16_t sKeys;
static bool sExitConfirmation;
static bool sHeldGameFrame;
static uint16_t sGbaImage[DISPLAY_WIDTH * MAX_RENDER_HEIGHT];
static uint16_t sPanelCache[SECONDARY_PANEL_WIDTH * SECONDARY_PANEL_HEIGHT];
static uint32_t sPanelRevision = UINT32_MAX;
static char sDataDirectory[1024] = RG_NANO_DATA_DIR;
static char sSavePath[1200];
static struct SiiRtcInfo sClock;

#ifdef RG_NANO_PROFILE_PPU
extern int64_t gPpuProfileBg;
extern int64_t gPpuProfileClear;
extern unsigned int gPpuTextBgCalls;
extern unsigned int gPpuAffineBgCalls;
extern int64_t gPpuProfileWin;
extern int64_t gPpuProfileObj;
extern int64_t gPpuProfileComposite;
#endif

extern unsigned int gVCountIntrFires;
extern bool gSecondaryPanelDisabled;
extern bool gSkipPixelRender;

// Frame-time attribution, reported and reset every [Perf] line.
static int64_t sProfilePpu;
static int64_t sProfileVBlank;
static int64_t sProfileConvert;
static int64_t sProfileFlip;
static int64_t sProfileWork;
static int64_t sProfileEvents;
static uint64_t sSkippedFrames;
static bool sVerboseLogging;
static uint64_t sProfileSamples;
static uint64_t sGameNotReady;

static int16_t sAudioRing[AUDIO_RING_SAMPLES];
static unsigned int sAudioRead;
static unsigned int sAudioWrite;
static unsigned int sAudioCount;
static unsigned int sAudioUnderruns;
static unsigned int sAudioOverflows;
static unsigned int sAudioQueueCalls;
static unsigned long long sAudioQueuedSamples;
static bool sAudioOpen;
static int sAudioDeviceRate = 44100;
static double sAudioResamplePos;
static bool32 sSkipAudioFrame;

// Index 9 is PLATFORM_SETTING_BATTLE_UI_TOP and must stay 1 (classic top-screen
// battle menus). At 0, DualScreen_BattleUiActive() turns on the Android port's
// bottom-screen takeover, whose FrameHook pins gBattle_BG0_Y to 0 every frame --
// but the battle controller scrolls that same register to DISPLAY_HEIGHT and
// DISPLAY_HEIGHT*2 to switch between the action and move menus, so pinning it
// shows the wrong menu band and flashes on every keypress. The takeover itself
// is entirely stubbed out in this port, so there is nothing to take over.
static u8 sPlatformSettings[PLATFORM_SETTING_COUNT] = {
    1, 1, 1, 1, 0, 10, 1, 0, 0, 1, 0, 0, 0, 0
};

static u8 BinToBcd(u8 value)
{
    u8 result = 0;
    int shift = 0;
    do
    {
        result |= (value % 10) << shift;
        value /= 10;
        shift += 4;
    } while (value != 0);
    return result;
}

static void UpdateInternalClock(void)
{
    time_t rawTime = time(NULL);
    struct tm local;
    static bool warned;
    localtime_r(&rawTime, &local);
    if (!warned && local.tm_year + 1900 < 2024)
    {
        fprintf(stderr, "[RTC] Drum78OS clock appears invalid: %04d-%02d-%02d\n",
                local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
        warned = true;
    }
    sClock.year = BinToBcd((u8)(local.tm_year - 100));
    sClock.month = BinToBcd((u8)(local.tm_mon + 1));
    sClock.day = BinToBcd((u8)local.tm_mday);
    sClock.dayOfWeek = BinToBcd((u8)local.tm_wday);
    sClock.hour = BinToBcd((u8)local.tm_hour);
    sClock.minute = BinToBcd((u8)local.tm_min);
    sClock.second = BinToBcd((u8)local.tm_sec);
}

static void SignalHandler(int signalNumber)
{
    (void)signalNumber;
    sRunning = 0;
}

// musl has no backtrace(), so log the faulting PC/LR instead. Feed them to
// arm-funkey-linux-musleabihf-addr2line -fe build/rg-nano/pokemon-emerald-nano
// to get file:line -- the shipped binary is stripped but has the same build ID.
static void CrashHandler(int signalNumber, siginfo_t *info, void *context)
{
    ucontext_t *uc = (ucontext_t *)context;
    fprintf(stderr,
            "[Crash] signal=%d addr=%p pc=%08lx lr=%08lx sp=%08lx r0=%08lx r1=%08lx r2=%08lx r3=%08lx\n",
            signalNumber,
            info != NULL ? info->si_addr : NULL,
            (unsigned long)uc->uc_mcontext.arm_pc,
            (unsigned long)uc->uc_mcontext.arm_lr,
            (unsigned long)uc->uc_mcontext.arm_sp,
            (unsigned long)uc->uc_mcontext.arm_r0,
            (unsigned long)uc->uc_mcontext.arm_r1,
            (unsigned long)uc->uc_mcontext.arm_r2,
            (unsigned long)uc->uc_mcontext.arm_r3);
    fflush(stderr);
    _exit(128 + signalNumber);
}

static void InstallCrashHandlers(void)
{
    static const int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    struct sigaction action;
    unsigned int i;

    memset(&action, 0, sizeof(action));
    action.sa_sigaction = CrashHandler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    for (i = 0; i < ARRAY_COUNT(signals); i++)
        sigaction(signals[i], &action, NULL);
}

static int EnsureDataDirectory(void)
{
    if (mkdir(sDataDirectory, 0755) == 0 || errno == EEXIST)
        return 0;
    fprintf(stderr, "Unable to create data directory %s: %s\n", sDataDirectory, strerror(errno));
    return -1;
}


static int OpenVideo(void)
{
    int page;

    sScreen = SDL_SetVideoMode(SECONDARY_PANEL_WIDTH,
                               DISPLAY_HEIGHT + SECONDARY_PANEL_HEIGHT,
                               16,
                               SDL_FULLSCREEN | SDL_HWSURFACE | SDL_DOUBLEBUF);
    if (sScreen == NULL)
    {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        return -1;
    }
    if (sScreen->format->BitsPerPixel != 16)
    {
        fprintf(stderr, "Expected 16-bit framebuffer, got %u\n", sScreen->format->BitsPerPixel);
        return -1;
    }
    SDL_ShowCursor(SDL_DISABLE);

    // Worth one line even in a release log: if SDL ever stops granting a
    // hardware double-buffered surface, SDL_Flip silently becomes a full
    // software copy and the frame budget goes with it.
    fprintf(stderr, "[Video] %dx%d %ubpp hw=%d doublebuf=%d\n",
            sScreen->w, sScreen->h, sScreen->format->BitsPerPixel,
            (sScreen->flags & SDL_HWSURFACE) ? 1 : 0,
            (sScreen->flags & SDL_DOUBLEBUF) ? 1 : 0);

    // /dev/fb0 is 240x720: three 240x240 pages. SDL_DOUBLEBUF only ever cycles
    // two of them, so the third keeps whatever the frontend last drew and
    // flashes back up whenever the driver pans to it. Paint every page black
    // once up front so no stale frontend image can surface later.
    for (page = 0; page < 4; page++)
    {
        if (SDL_MUSTLOCK(sScreen) && SDL_LockSurface(sScreen) != 0)
            break;
        memset(sScreen->pixels, 0, (size_t)sScreen->pitch * sScreen->h);
        if (SDL_MUSTLOCK(sScreen))
            SDL_UnlockSurface(sScreen);
        SDL_Flip(sScreen);
    }
    return 0;
}

static void RenderGate(void)
{
    if (SDL_MUSTLOCK(sScreen) && SDL_LockSurface(sScreen) != 0)
        return;
    SecondaryPanel_RenderFullScreen(SecondaryPanel_GetModel(),
                                    (uint16_t *)sScreen->pixels,
                                    sScreen->pitch / (int)sizeof(uint16_t),
                                    sScreen->h);
    if (SDL_MUSTLOCK(sScreen))
        SDL_UnlockSurface(sScreen);
    SDL_Flip(sScreen);
}

static int RunAssetGate(const char *manifestPath)
{
    char detail[128];
    int result;
    for (;;)
    {
        result = RgNanoAssetGate_Fill(sDataDirectory, manifestPath, detail, sizeof(detail));
        if (result >= 0)
        {
            fprintf(stderr, "[Assets] %s\n", RgNanoAssetGate_ResultText(result));
            return 0;
        }
        // No third line: SecondaryPanel_RenderFullScreen draws the
        // "A RETRY   B EXIT" prompt itself, so passing it here printed it twice.
        SecondaryPanel_SetSystemMode(SECONDARY_PANEL_GATE,
                                     RgNanoAssetGate_ResultText(result),
                                     detail,
                                     "");
        RenderGate();
        while (sRunning)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_QUIT)
                    return -1;
                if (event.type == SDL_KEYDOWN)
                {
                    if (event.key.keysym.sym == SDLK_a)
                        goto retry;
                    if (event.key.keysym.sym == SDLK_b || event.key.keysym.sym == SDLK_q)
                        return -1;
                }
            }
            SDL_Delay(20);
        }
        return -1;
retry:
        continue;
    }
}

static void ReadSaveFile(void)
{
    FILE *file = fopen(sSavePath, "rb");
    size_t read = 0;
    memset(FLASH_BASE, 0xFF, sizeof(FLASH_BASE));
    if (file != NULL)
    {
        read = fread(FLASH_BASE, 1, sizeof(FLASH_BASE), file);
        fclose(file);
    }
    fprintf(stderr, "[Save] loaded %u bytes from %s\n", (unsigned)read, sSavePath);
}

static int StoreSaveFileAtomic(void)
{
    char temporary[1240];
    FILE *file;
    int fd;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", sSavePath) >= (int)sizeof(temporary))
        return -1;
    file = fopen(temporary, "wb");
    if (file == NULL)
        return -1;
    if (fwrite(FLASH_BASE, 1, sizeof(FLASH_BASE), file) != sizeof(FLASH_BASE)
     || fflush(file) != 0)
    {
        fclose(file);
        unlink(temporary);
        return -1;
    }
    fd = fileno(file);
    if (fsync(fd) != 0 || fclose(file) != 0 || rename(temporary, sSavePath) != 0)
    {
        unlink(temporary);
        return -1;
    }
    return 0;
}

static void AudioCallback(void *userData, Uint8 *stream, int length)
{
    int16_t *output = (int16_t *)stream;
    unsigned int samples = (unsigned int)length / sizeof(*output);
    unsigned int i;
    (void)userData;
    for (i = 0; i < samples; i++)
    {
        if (sAudioCount == 0)
        {
            output[i] = 0;
            sAudioUnderruns++;
        }
        else
        {
            output[i] = sAudioRing[sAudioRead];
            sAudioRead = (sAudioRead + 1) % AUDIO_RING_SAMPLES;
            sAudioCount--;
        }
    }
}

static int OpenAudio(void)
{
    SDL_AudioSpec wanted;
    SDL_AudioSpec obtained;
    memset(&wanted, 0, sizeof(wanted));
    memset(&obtained, 0, sizeof(obtained));
    // The m4a mixer is hard-wired to SOUND_MODE_FREQ_42048 and emits exactly
    // one frame of samples at that rate per game frame, so the device has to
    // run at the same rate or it consumes faster than the game can produce.
    // Asking for 44100 left it ~5% short every single frame, which drains the
    // ring buffer and underruns forever. Upstream's SDL2 backend asks for
    // 42060 for the same reason.
    // Ask for a rate the V3s codec runs natively. Requesting 42060 (the m4a
    // mixer's own rate) got reported back as granted, but measurement says the
    // hardware actually drains ~44100 samples/sec regardless: per 600 frames the
    // game queued 841200 samples and lost another 65418 to underruns, which only
    // adds up at 44100. So the stream was played ~4.85% fast and starved by the
    // same margin forever, at any frame rate. Open at the real rate and convert
    // properly in Platform_QueueAudio instead.
    wanted.freq = 44100;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = 1024;
    wanted.callback = AudioCallback;
    if (SDL_OpenAudio(&wanted, &obtained) != 0)
    {
        fprintf(stderr, "SDL_OpenAudio failed: %s\n", SDL_GetError());
        cgb_audio_init(44100);
        return -1;
    }
    if (obtained.format != AUDIO_S16SYS || obtained.channels != 2)
    {
        fprintf(stderr, "Unsupported audio format: format=%04x channels=%u\n",
                obtained.format, obtained.channels);
        SDL_CloseAudio();
        cgb_audio_init(44100);
        return -1;
    }
    fprintf(stderr, "[Audio] %d Hz S16 stereo, %u-frame callback\n",
            obtained.freq, obtained.samples);
    sAudioDeviceRate = obtained.freq > 0 ? obtained.freq : 44100;
    cgb_audio_init(obtained.freq);
    SDL_PauseAudio(0);
    sAudioOpen = true;
    return 0;
}

static uint16_t KeyMask(SDLKey key)
{
    switch (key)
    {
    case SDLK_a: return A_BUTTON;
    case SDLK_b: return B_BUTTON;
    case SDLK_s: return START_BUTTON;
    case SDLK_k: return SELECT_BUTTON;
    // L (m) and R (n) are handled in ProcessEvents as companion-panel controls
    // rather than GBA shoulder buttons; Emerald barely uses them.
    case SDLK_u: return DPAD_UP;
    case SDLK_d: return DPAD_DOWN;
    case SDLK_l: return DPAD_LEFT;
    case SDLK_r: return DPAD_RIGHT;
    default: return 0;
    }
}

static void ProcessEvents(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT)
        {
            sRunning = 0;
            continue;
        }
        if (event.type == SDL_KEYDOWN)
        {
            if (event.key.keysym.sym == SDLK_q)
            {
                sKeys = 0;
                sExitConfirmation = true;
                SecondaryPanel_SetSystemMode(SECONDARY_PANEL_EXIT_CONFIRMATION,
                                             "EXIT TO LAUNCHER?", "A YES", "B NO");
                continue;
            }
            if (sExitConfirmation)
            {
                if (event.key.keysym.sym == SDLK_a)
                    sRunning = 0;
                else if (event.key.keysym.sym == SDLK_b)
                {
                    sExitConfirmation = false;
                    SecondaryPanel_ClearSystemMode();
                    if (sHeldGameFrame)
                    {
                        SDL_SemPost(sVBlank);
                        sHeldGameFrame = false;
                    }
                }
                continue;
            }
#if RG_NANO_FULLSCREEN
            // X toggles the widened frame, Y re-reads fullscreen240.cfg so the
            // margins can be dialled in over SSH without a rebuild. Neither
            // button is a GBA button (see KeyMask), so nothing is stolen from
            // the game.
            if (event.key.keysym.sym == SDLK_x)
            {
                Fullscreen240_Toggle();
                continue;
            }
            if (event.key.keysym.sym == SDLK_y)
            {
                Fullscreen240_CycleZoom();
                continue;
            }
#endif
            if (event.key.keysym.sym == SDLK_m)
            {
                SecondaryPanel_CycleView(-1);
                continue;
            }
            if (event.key.keysym.sym == SDLK_n)
            {
                SecondaryPanel_CycleView(1);
                continue;
            }
            sKeys |= KeyMask(event.key.keysym.sym);
        }
        else if (event.type == SDL_KEYUP && !sExitConfirmation)
        {
            sKeys &= ~KeyMask(event.key.keysym.sym);
        }
    }
}

static void RunVBlank(void)
{
    REG_DISPSTAT |= INTR_FLAG_VBLANK;
    RunDMAs(DMA_HBLANK);
    // Gate on REG_IE rather than DISPSTAT's V-blank IRQ enable bit. The game
    // turns the interrupt on with EnableInterrupts(INTR_FLAG_VBLANK), which
    // updates REG_IE immediately but only reaches DISPSTAT through the
    // buffered GPU register manager -- and that buffer is flushed by
    // CopyBufferedValuesToGpuRegs() from inside VBlankIntr itself. Gating on
    // DISPSTAT deadlocks at boot: the handler never runs, so the bit that
    // would enable it is never written, leaving a white screen and silence.
    // Upstream's Android backend gates on REG_IE for the same reason.
    if (REG_IE & INTR_FLAG_VBLANK)
        gIntrTable[4]();
    REG_DISPSTAT &= ~INTR_FLAG_VBLANK;
}

static int64_t MonotonicNs(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
}

#if RG_NANO_FULLSCREEN
// The rendered region is gRenderWidth x gRenderHeight of GBA screen space and
// the panel is always 240x240, so the composition step magnifies. Both are
// square and GBA pixels are square, so this is a uniform scale -- nothing is
// stretched. Nearest neighbour, via two precomputed index tables so the inner
// loop stays a load and a shift; rebuilt only when the source size changes,
// which is a handful of frames per zoom.
static uint16_t sScaleX[DISPLAY_WIDTH];
static uint16_t sScaleY[MAX_RENDER_HEIGHT];
static int sScaleSrcWidth;
static int sScaleSrcHeight;

static int sScaleDestHeight;

static void UpdateScaleTables(int srcWidth, int srcHeight, int destHeight)
{
    int i;

    if (srcWidth == sScaleSrcWidth && srcHeight == sScaleSrcHeight
     && destHeight == sScaleDestHeight)
        return;
    for (i = 0; i < DISPLAY_WIDTH; i++)
        sScaleX[i] = (uint16_t)(((2 * i + 1) * srcWidth) / (2 * DISPLAY_WIDTH));
    for (i = 0; i < destHeight && i < MAX_RENDER_HEIGHT; i++)
        sScaleY[i] = (uint16_t)(((2 * i + 1) * srcHeight) / (2 * destHeight));
    sScaleSrcWidth = srcWidth;
    sScaleSrcHeight = srcHeight;
    sScaleDestHeight = destHeight;
}

// BGR555 -> RGB565 by arithmetic. The old 32768-entry lookup table was 64KB
// against a 32KB L1, so a large share of lookups missed to main memory -- far
// more expensive than the handful of shifts it replaced.
static inline uint16_t ConvertPixel(unsigned int color)
{
    unsigned int red = color & 0x1F;
    unsigned int green = (color >> 5) & 0x1F;
    unsigned int blue = (color >> 10) & 0x1F;

    // 5-bit green -> 6 bits, replicating the top bit like the old table.
    return (uint16_t)((red << 11) | (((green << 1) | (green >> 4)) << 5) | blue);
}
#endif

static void DrawComposedFrame(bool runGameVBlank)
{
    const struct SecondaryPanelModel *model;
    uint16_t *screenPixels;
    int64_t drawStart;
    int64_t ppuEnd;
    int64_t convertStart;
    int64_t flipStart;
    int64_t flipEnd;
    int pitch;
    int x;
    int y;

    drawStart = MonotonicNs();
    ppuEnd = drawStart;
    if (runGameVBlank)
    {
#if RG_NANO_FULLSCREEN
        // Must happen here and nowhere else: the game thread is parked in
        // VBlankIntrWait for the whole of this function, so this is the only
        // point where the render geometry can change without tearing a frame
        // between two different heights.
        Fullscreen240_Update();
#endif
        if (!gSkipPixelRender)
            memset(sGbaImage, 0, sizeof(sGbaImage));
        DrawFrame(sGbaImage);
        ppuEnd = MonotonicNs();
        REG_VCOUNT = 161;
        // Must be the last write to gBattle_BG0_Y before the V-blank handler
        // copies it into REG_BG0VOFS; see SecondaryPanel_HideTopBattleMenu.
        SecondaryPanel_HideTopBattleMenu();
        RunVBlank();
        DualScreen_FrameHook();
    }
    convertStart = MonotonicNs();
    if (gSkipPixelRender)
    {
        // No new pixels, so there is nothing to convert and nothing to show:
        // leave the current page on screen. Skipping the flip is most of what
        // makes a skipped frame cheap.
        sProfilePpu += ppuEnd - drawStart;
        sProfileVBlank += convertStart - ppuEnd;
        sProfileSamples++;
        sSkippedFrames++;
        return;
    }
    model = SecondaryPanel_GetModel();
    if (!gSecondaryPanelDisabled
     && (model->revision != sPanelRevision || sExitConfirmation))
    {
        SecondaryPanel_Render(model, sPanelCache, SECONDARY_PANEL_WIDTH);
        sPanelRevision = model->revision;
    }
    if (SDL_MUSTLOCK(sScreen) && SDL_LockSurface(sScreen) != 0)
        return;
    screenPixels = (uint16_t *)sScreen->pixels;
    pitch = sScreen->pitch / (int)sizeof(uint16_t);
    // Convert BGR555 -> RGB565 arithmetically. The old 32768-entry lookup table
    // was 64KB against a 32KB L1, so a large share of lookups missed to main
    // memory -- far more expensive than the handful of shifts it replaced.
#if RG_NANO_FULLSCREEN
    if (Fullscreen240_Active())
    {
        // The 1:1 view is capped at 208 rows by the map buffer (see
        // fullscreen240.c), so it is centred and letterboxed rather than
        // stretched to 240 -- a 240/208 magnification is non-integer, and
        // those were rejected on this panel.
        int destHeight = Fullscreen240_DestHeight();
        int destTop = (MAX_RENDER_HEIGHT - destHeight) / 2;

        UpdateScaleTables(gRenderWidth, gRenderHeight, destHeight);
        for (y = 0; y < destTop; y++)
            memset(screenPixels + y * pitch, 0, DISPLAY_WIDTH * sizeof(uint16_t));
        for (y = destTop + destHeight; y < MAX_RENDER_HEIGHT; y++)
            memset(screenPixels + y * pitch, 0, DISPLAY_WIDTH * sizeof(uint16_t));

        if (gRenderWidth == DISPLAY_WIDTH && gRenderHeight == destHeight)
        {
            // 1:1. Worth its own loop: this is the mode with the least frame
            // budget to spare, so it does not pay for the index tables.
            for (y = 0; y < destHeight; y++)
            {
                const uint16_t *restrict src = sGbaImage + y * DISPLAY_WIDTH;
                uint16_t *restrict dst = screenPixels + (destTop + y) * pitch;

                for (x = 0; x < DISPLAY_WIDTH; x++)
                    dst[x] = ConvertPixel(src[x]);
            }
        }
        else
        {
            for (y = 0; y < destHeight; y++)
            {
                const uint16_t *restrict src = sGbaImage + sScaleY[y] * gRenderWidth;
                uint16_t *restrict dst = screenPixels + (destTop + y) * pitch;

                for (x = 0; x < DISPLAY_WIDTH; x++)
                    dst[x] = ConvertPixel(src[sScaleX[x]]);
            }
        }
    }
    else
#endif
    for (y = 0; y < gRenderHeight; y++)
    {
        const uint16_t *restrict src = sGbaImage + y * gRenderWidth;
        uint16_t *restrict dst = screenPixels + y * pitch;

        for (x = 0; x < DISPLAY_WIDTH; x++)
        {
            unsigned int color = src[x];
            unsigned int red = color & 0x1F;
            unsigned int green = (color >> 5) & 0x1F;
            unsigned int blue = (color >> 10) & 0x1F;

            // 5-bit green -> 6 bits, replicating the top bit like the old table.
            dst[x] = (uint16_t)((red << 11) | (((green << 1) | (green >> 4)) << 5) | blue);
        }
    }
    // The widened frame reaches the bottom of the panel, so there is no room
    // left for the companion panel and no blit to do. Dropping back to the
    // stock frame repaints it from the cache on the very next frame, and both
    // SDL pages are covered within two.
    // The exit prompt is drawn by the companion panel, so the panel has to come
    // back for it even when the experiment owns the whole screen -- otherwise
    // MENU halts the game waiting for an A/B that the player cannot see.
    if (!gSecondaryPanelDisabled && (!Fullscreen240_Active() || sExitConfirmation))
    {
        for (y = 0; y < SECONDARY_PANEL_HEIGHT; y++)
            memcpy(screenPixels + (DISPLAY_HEIGHT + y) * pitch,
                   sPanelCache + y * SECONDARY_PANEL_WIDTH,
                   SECONDARY_PANEL_WIDTH * sizeof(uint16_t));
    }
    if (SDL_MUSTLOCK(sScreen))
        SDL_UnlockSurface(sScreen);
    flipStart = MonotonicNs();
    SDL_Flip(sScreen);
    flipEnd = MonotonicNs();

    sProfilePpu += ppuEnd - drawStart;
    sProfileVBlank += convertStart - ppuEnd;
    sProfileConvert += flipStart - convertStart;
    sProfileFlip += flipEnd - flipStart;
    sProfileSamples++;
}

static int GameThread(void *unused)
{
    (void)unused;
    AgbMain();
    return 0;
}

static int64_t TimespecNs(const struct timespec *time)
{
    return (int64_t)time->tv_sec * 1000000000LL + time->tv_nsec;
}

static void AddFrameTime(struct timespec *time)
{
    time->tv_nsec += RG_NANO_FRAME_NS;
    if (time->tv_nsec >= 1000000000L)
    {
        time->tv_sec++;
        time->tv_nsec -= 1000000000L;
    }
}

int main(int argc, char **argv)
{
    const char *manifestPath = "asset_manifest.bin";
    struct timespec deadline;
    uint64_t frameCount = 0;
    uint64_t lateFrames = 0;
    uint64_t loggedFrame = 0;
    int argument;

    for (argument = 1; argument < argc; argument++)
    {
        if (strcmp(argv[argument], "--data-dir") == 0 && argument + 1 < argc)
            snprintf(sDataDirectory, sizeof(sDataDirectory), "%s", argv[++argument]);
        else if (strcmp(argv[argument], "--asset-manifest") == 0 && argument + 1 < argc)
            manifestPath = argv[++argument];
    }
    if (EnsureDataDirectory() != 0)
        return 1;
    if (snprintf(sSavePath, sizeof(sSavePath), "%s/pokeemerald.sav", sDataDirectory) >= (int)sizeof(sSavePath))
        return 1;

    // Two opt-in markers in the data dir, both off by default:
    //   nopanel - run without the companion panel (used to measure its cost)
    //   debug   - per-600-frame timing and audio counters on stderr
    // A release run logs only startup state, saves, and crashes.
    {
        char flagPath[1240];

        snprintf(flagPath, sizeof(flagPath), "%s/nopanel", sDataDirectory);
        gSecondaryPanelDisabled = (access(flagPath, F_OK) == 0);
        if (gSecondaryPanelDisabled)
            fprintf(stderr, "[Panel] companion panel DISABLED (nopanel present)\n");

#if RG_NANO_FULLSCREEN
        Fullscreen240_Init(sDataDirectory);
#endif
        snprintf(flagPath, sizeof(flagPath), "%s/debug", sDataDirectory);
        sVerboseLogging = (access(flagPath, F_OK) == 0);
        if (sVerboseLogging)
            fprintf(stderr, "[Debug] verbose frame logging enabled (debug present)\n");
    }

    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    InstallCrashHandlers();
    SecondaryPanel_Reset();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (OpenVideo() != 0)
        return 1;
    if (RunAssetGate(manifestPath) != 0)
    {
        SDL_Quit();
        return 2;
    }
    ModManager_Init();
    SecondaryPanel_ClearSystemMode();
    SecondaryPanel_Reset();
    ReadSaveFile();
    OpenAudio();

    memset(&sClock, 0, sizeof(sClock));
    sClock.status = SIIRTCINFO_24HOUR;
    UpdateInternalClock();
    sFrameReady = SDL_CreateSemaphore(0);
    sVBlank = SDL_CreateSemaphore(0);
    if (sFrameReady == NULL || sVBlank == NULL)
    {
        fprintf(stderr, "Unable to create frame semaphores: %s\n", SDL_GetError());
        return 1;
    }
    sGameThread = SDL_CreateThread(GameThread, NULL);
    if (sGameThread == NULL)
    {
        fprintf(stderr, "Unable to create game thread: %s\n", SDL_GetError());
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &deadline);
    while (sRunning)
    {
        struct timespec now;
        int64_t eventsStart;
        int64_t workStart;

        // The per-stage numbers only cover DrawComposedFrame. Measure the whole
        // iteration too: the stages sum to ~1.1ms less than the real frame time,
        // so something outside them is costing that and needs finding.
        eventsStart = MonotonicNs();
        ProcessEvents();
        sProfileEvents += MonotonicNs() - eventsStart;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (TimespecNs(&now) < TimespecNs(&deadline))
        {
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
            clock_gettime(CLOCK_MONOTONIC, &now);
        }
        if (TimespecNs(&now) - TimespecNs(&deadline) > 20000000LL)
            lateFrames++;

        // Hold true 60Hz game and audio timing by dropping the redraw (not the
        // frame) once we have fallen a whole frame behind. The scanline pass
        // still runs, so game logic, interrupts and audio are unaffected; only
        // the picture is not refreshed that once. Never two in a row, so motion
        // stays smooth.
        gSkipPixelRender = !gSkipPixelRender
                        && (TimespecNs(&now) - TimespecNs(&deadline)) >= RG_NANO_FRAME_NS;
        workStart = MonotonicNs();

        if (sExitConfirmation)
        {
            if (!sHeldGameFrame && SDL_SemTryWait(sFrameReady) == 0)
            {
                sHeldGameFrame = true;
                DrawComposedFrame(true);
            }
            else
            {
                DrawComposedFrame(false);
            }
        }
        else if (SDL_SemTryWait(sFrameReady) == 0)
        {
            DrawComposedFrame(true);
            SDL_SemPost(sVBlank);
            frameCount++;
        }
        else
        {
            // The game thread has not reached VBlankIntrWait yet, so this
            // iteration draws nothing: game logic, not the platform, is what
            // is behind.
            sGameNotReady++;
        }
        sProfileWork += MonotonicNs() - workStart;

        AddFrameTime(&deadline);
        if (TimespecNs(&now) - TimespecNs(&deadline) > RG_NANO_FRAME_NS * 5LL)
            deadline = now;
        if (sVerboseLogging
         && frameCount != 0 && frameCount % 600 == 0 && frameCount != loggedFrame)
        {
            uint64_t samples = sProfileSamples != 0 ? sProfileSamples : 1;
            loggedFrame = frameCount;
            fprintf(stderr,
                    "[Perf] frames=%llu late=%llu stalled=%llu ppu=%lldus vblank=%lldus conv=%lldus flip=%lldus audio_under=%u audio_over=%u\n",
                    (unsigned long long)frameCount, (unsigned long long)lateFrames,
                    (unsigned long long)sGameNotReady,
                    (long long)(sProfilePpu / (int64_t)samples / 1000),
                    (long long)(sProfileVBlank / (int64_t)samples / 1000),
                    (long long)(sProfileConvert / (int64_t)samples / 1000),
                    (long long)(sProfileFlip / (int64_t)samples / 1000),
                    sAudioUnderruns, sAudioOverflows);
            // work = the whole draw path; the gap against the four stages above
            // is time nothing accounts for. events = ProcessEvents.
            fprintf(stderr,
                    "[Loop]  skipped_redraws=%llu work=%lldus events=%lldus unaccounted=%lldus\n",
                    (unsigned long long)sSkippedFrames,
                    (long long)(sProfileWork / (int64_t)samples / 1000),
                    (long long)(sProfileEvents / (int64_t)samples / 1000),
                    (long long)((sProfileWork - sProfilePpu - sProfileVBlank
                                 - sProfileConvert - sProfileFlip)
                                / (int64_t)samples / 1000));
            sProfileWork = 0;
            sProfileEvents = 0;
            sSkippedFrames = 0;
#if RG_NANO_FULLSCREEN
            {
                extern u32 gExperimentObjectsPeak;

                fprintf(stderr, "[Objects] peak_active=%u/%u\n",
                        gExperimentObjectsPeak, OBJECT_EVENTS_COUNT);
                gExperimentObjectsPeak = 0;
            }
#endif
            fprintf(stderr,
                    "[Audio] queue_calls=%u queued_samples=%llu ring=%u vcount_fires=%u dispstat=%04x ie=%04x\n",
                    sAudioQueueCalls, sAudioQueuedSamples, sAudioCount,
                    gVCountIntrFires, (unsigned)REG_DISPSTAT, (unsigned)REG_IE);
            sAudioQueueCalls = 0;
            sAudioQueuedSamples = 0;
            gVCountIntrFires = 0;
#ifdef RG_NANO_PROFILE_PPU
            fprintf(stderr,
                    "[PPU]  clear=%lldus bg=%lldus win=%lldus obj=%lldus composite=%lldus text_calls=%u affine_calls=%u\n",
                    (long long)(gPpuProfileClear / (int64_t)samples / 1000),
                    (long long)(gPpuProfileBg / (int64_t)samples / 1000),
                    (long long)(gPpuProfileWin / (int64_t)samples / 1000),
                    (long long)(gPpuProfileObj / (int64_t)samples / 1000),
                    (long long)(gPpuProfileComposite / (int64_t)samples / 1000),
                    (unsigned)(gPpuTextBgCalls / (unsigned)samples),
                    (unsigned)(gPpuAffineBgCalls / (unsigned)samples));
            gPpuProfileBg = 0;
            gPpuProfileClear = 0;
            gPpuTextBgCalls = 0;
            gPpuAffineBgCalls = 0;
            gPpuProfileWin = 0;
            gPpuProfileObj = 0;
            gPpuProfileComposite = 0;
#endif
            sProfilePpu = 0;
            sProfileVBlank = 0;
            sProfileConvert = 0;
            sProfileFlip = 0;
            sProfileSamples = 0;
            sGameNotReady = 0;
        }
    }

    Platform_StoreSaveFile();
    fprintf(stderr, "[Exit] frames=%llu late=%llu audio_under=%u audio_over=%u\n",
            (unsigned long long)frameCount, (unsigned long long)lateFrames,
            sAudioUnderruns, sAudioOverflows);
    SDL_CloseAudio();
    SDL_Quit();
    return 0;
}

void VBlankIntrWait(void)
{
    SDL_SemPost(sFrameReady);
    SDL_SemWait(sVBlank);
}

void Platform_StoreSaveFile(void)
{
    if (StoreSaveFileAtomic() != 0)
        fprintf(stderr, "[Save] atomic write failed: %s\n", strerror(errno));
}

void Platform_ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size)
{
    u32 address = ((u32)sectorNum << gFlash->sector.shift) + offset;
    if (address <= sizeof(FLASH_BASE) && size <= sizeof(FLASH_BASE) - address)
        memcpy(dest, FLASH_BASE + address, size);
}

void Platform_QueueAudio(float *audioBuffer, s32 samplesPerFrame)
{
    unsigned int sampleCount = samplesPerFrame > 0 ? (unsigned int)samplesPerFrame / sizeof(float) : 0;
    unsigned int srcFrames;
    double outFrames;
    double step;
    // The mixer's non-Android path scales its output by 0.125 where Android
    // uses 0.5, which on this speaker is far too quiet. Make up the same 4x
    // here rather than editing shared mixer code; the clamp below still guards
    // against clipping.
    float volume = (sPlatformSettings[PLATFORM_SETTING_VOLUME] / 10.0f) * 4.0f;
    if (!sAudioOpen)
        return;
    srcFrames = sampleCount / 2;
    if (srcFrames == 0)
        return;
    sAudioQueueCalls++;

    // Fixed-ratio sample rate conversion, not a feedback loop: the mixer emits
    // one frame's worth of audio at its own fixed rate, and the device wants
    // exactly (deviceRate / framesPerSecond) frames in that time. Both sides are
    // constants, so the ratio is a constant too -- the pitch cannot drift.
    outFrames = (double)sAudioDeviceRate * (RG_NANO_FRAME_NS / 1000000000.0);
    step = (double)srcFrames / outFrames;

    SDL_LockAudio();
    {
        double pos = sAudioResamplePos;

        while (pos < (double)srcFrames)
        {
            unsigned int i = (unsigned int)pos;
            float frac = (float)(pos - (double)i);
            unsigned int j = (i + 1 < srcFrames) ? i + 1 : i;
            int channel;

            for (channel = 0; channel < 2; channel++)
            {
                float a = audioBuffer[i * 2 + channel];
                float b = audioBuffer[j * 2 + channel];
                float value = (a + (b - a) * frac) * volume;
                int converted;

                if (value > 1.0f) value = 1.0f;
                if (value < -1.0f) value = -1.0f;
                converted = (int)(value * 32767.0f);
                if (sAudioCount == AUDIO_RING_SAMPLES)
                {
                    sAudioRead = (sAudioRead + 1) % AUDIO_RING_SAMPLES;
                    sAudioCount--;
                    sAudioOverflows++;
                }
                sAudioRing[sAudioWrite] = (int16_t)converted;
                sAudioWrite = (sAudioWrite + 1) % AUDIO_RING_SAMPLES;
                sAudioCount++;
                sAudioQueuedSamples++;
            }
            pos += step;
        }
        // Carry the fraction so block boundaries do not drop or repeat a sample.
        sAudioResamplePos = pos - (double)srcFrames;
    }
    SDL_UnlockAudio();
}

bool32 Platform_SkipAudioFrame(void)
{
    return sSkipAudioFrame;
}

u16 Platform_GetKeyInput(void)
{
    return sKeys | DualScreen_ConsumeVirtualKeys();
}

u8 Platform_GetBorderBackgroundCount(void)
{
    return 1;
}

u8 Platform_GetBorderBackground(void)
{
    return 0;
}

void Platform_SetBorderBackground(u8 selection)
{
    (void)selection;
}

u8 Platform_GetSetting(enum PlatformSetting setting)
{
    if (setting >= PLATFORM_SETTING_COUNT)
        return 0;
    return sPlatformSettings[setting];
}

void Platform_SetSetting(enum PlatformSetting setting, u8 value)
{
    if (setting >= PLATFORM_SETTING_COUNT)
        return;
    if (setting == PLATFORM_SETTING_VOLUME)
        sPlatformSettings[setting] = value > 10 ? 10 : value;
    else if (setting == PLATFORM_SETTING_BATTLE_UI_TOP || setting == PLATFORM_SETTING_BATTLE_HINTS)
        sPlatformSettings[setting] = value != 0;
}

void Platform_GetStatus(struct SiiRtcInfo *rtc)
{
    rtc->status = sClock.status;
}

void Platform_SetStatus(struct SiiRtcInfo *rtc)
{
    sClock.status = rtc->status;
}

void Platform_GetDateTime(struct SiiRtcInfo *rtc)
{
    UpdateInternalClock();
    *rtc = sClock;
}

void Platform_SetDateTime(struct SiiRtcInfo *rtc)
{
    sClock = *rtc;
}

void Platform_GetTime(struct SiiRtcInfo *rtc)
{
    UpdateInternalClock();
    rtc->hour = sClock.hour;
    rtc->minute = sClock.minute;
    rtc->second = sClock.second;
}

void Platform_SetTime(struct SiiRtcInfo *rtc)
{
    sClock.hour = rtc->hour;
    sClock.minute = rtc->minute;
    sClock.second = rtc->second;
}

void Platform_SetAlarm(u8 *alarmData)
{
    (void)alarmData;
}

void SoftReset(u32 resetFlags)
{
    (void)resetFlags;
    Platform_StoreSaveFile();
    sRunning = 0;
}

#endif
