#ifndef GUARD_EXPERIMENT_FULLSCREEN240_H
#define GUARD_EXPERIMENT_FULLSCREEN240_H

// experiments/fullscreen240 -- fill the RG Nano's whole 240x240 panel with the
// game at 1:1 instead of a 240x160 game area plus the 240x80 companion panel.
//
// Compiled only when the build sets RG_NANO_FULLSCREEN=1; see README.md.

#if RG_NANO_FULLSCREEN

#include "global.h"

// Platform layer.
void Fullscreen240_Init(const char *dataDir);   // once, at startup
void Fullscreen240_Update(void);                // once per frame, game parked
void Fullscreen240_Toggle(void);                // X button
void Fullscreen240_CycleZoom(void);              // Y button
bool32 Fullscreen240_Active(void);              // is the widened frame live now
int Fullscreen240_DestHeight(void);             // rows the frame occupies on the panel
bool32 Fullscreen240_UiOverlay(void);           // draw BG0 unscaled over the world
int Fullscreen240_UiBoxTop(void);               // dialogue box edges in BG0 coordinates
int Fullscreen240_UiBoxBottom(void);
const char *Fullscreen240_StatusLine(void);     // for the log

// Game side. Tile rows to push the standard dialogue box down by, so it sits on
// the bottom edge of the widened frame instead of floating in the middle of it.
// 0 whenever the widened frame is not active.
int Fullscreen240_TextBoxShiftRows(void);

#else

#define Fullscreen240_TextBoxShiftRows() 0
#define Fullscreen240_Active()           FALSE
#define Fullscreen240_DestHeight()       DISPLAY_HEIGHT
#define Fullscreen240_UiOverlay()        FALSE

#endif // RG_NANO_FULLSCREEN

#endif // GUARD_EXPERIMENT_FULLSCREEN240_H
