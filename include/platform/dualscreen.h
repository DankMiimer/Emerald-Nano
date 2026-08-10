#ifndef GUARD_PLATFORM_DUALSCREEN_H
#define GUARD_PLATFORM_DUALSCREEN_H

// Second-screen state bridge. Called from the SDL frame loop each vblank,
// after the game's vblank interrupt handler has run and before the game
// thread is released. Snapshots live game state into a JSON buffer that the
// Android bottom-screen UI (or a desktop debug consumer) reads.
void DualScreen_FrameHook(void);

// Returns the most recent snapshot as a NUL-terminated JSON string.
// Thread-safe; may be called from any thread.
const char *DualScreen_GetSnapshotJson(void);

#endif // GUARD_PLATFORM_DUALSCREEN_H
