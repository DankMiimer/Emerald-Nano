#ifndef GUARD_MOD_MANAGER_H
#define GUARD_MOD_MANAGER_H

#include "global.h"
#include "data.h"
#include <stdbool.h>

extern bool8 gModsEnabled;

void ModManager_Init(void);
void ModManager_Shutdown(void);
bool8 ModManager_IsEnabled(void);

// Trainer Override API
const struct Trainer *ModManager_GetTrainer(u16 trainerId);

// Graphics Override API
// Returns TRUE if overridden and copies the raw 4bpp uncompressed data into destBuffer.
bool8 ModManager_GetTrainerFrontPicOverride(u16 trainerPicId, void *destBuffer);


// Starter Override API
u16 ModManager_GetStarterSpecies(u8 slot, u16 vanillaSpecies);
u8 ModManager_GetStarterLevel(u8 slot, u8 vanillaLevel);

#endif // GUARD_MOD_MANAGER_H

