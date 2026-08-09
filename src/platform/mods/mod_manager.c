#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "cJSON.h"
#include "mod_manager.h"
#include "constants/trainers.h"

#ifdef NATIVE_LINUX

bool8 gModsEnabled = FALSE;

#define MAX_LOADED_MODS 32
#define MAX_TRAINER_OVERRIDES 128

typedef struct {
    char id[64];
    int priority;
    char path[256];
} LoadedMod;

typedef struct {
    u16 trainerId;
    struct Trainer data;
    struct TrainerMonItemCustomMoves party[6];
} TrainerOverride;

typedef struct {
    u8 slot;
    u16 species;
    u8 level;
} StarterOverride;

static LoadedMod sLoadedMods[MAX_LOADED_MODS];
static int sNumLoadedMods = 0;

static TrainerOverride sTrainerOverrides[MAX_TRAINER_OVERRIDES];
static int sNumTrainerOverrides = 0;

static StarterOverride sStarterOverrides[3];
static int sNumStarterOverrides = 0;

static int CompareMods(const void *a, const void *b) {
    const LoadedMod *ma = (const LoadedMod *)a;
    const LoadedMod *mb = (const LoadedMod *)b;
    // Higher priority first
    if (ma->priority != mb->priority) {
        return mb->priority - ma->priority; 
    }
    // Deterministic tie-breaker
    return strcmp(ma->id, mb->id);
}

static char* ReadFileToString(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buffer = malloc(length + 1);
    if (buffer) {
        fread(buffer, 1, length, f);
        buffer[length] = '\0';
    }
    fclose(f);
    return buffer;
}

static void LoadTrainerOverrides(LoadedMod *mod) {
    char path[512];
    snprintf(path, sizeof(path), "%s/data/trainers.json", mod->path);
    char *jsonStr = ReadFileToString(path);
    if (!jsonStr) return;

    cJSON *root = cJSON_Parse(jsonStr);
    if (!root) {
        fprintf(stderr, "[Mods][ERROR] Invalid JSON in %s\n", path);
        free(jsonStr);
        return;
    }

    cJSON *trainers = cJSON_GetObjectItem(root, "trainers");
    if (cJSON_IsArray(trainers)) {
        cJSON *trainer;
        cJSON_ArrayForEach(trainer, trainers) {
            cJSON *idObj = cJSON_GetObjectItem(trainer, "id");
            if (!cJSON_IsNumber(idObj)) continue;
            u16 tId = idObj->valueint;

            // Check if already overridden (higher priority mod would have done it)
            bool8 alreadyOverridden = FALSE;
            for (int i = 0; i < sNumTrainerOverrides; i++) {
                if (sTrainerOverrides[i].trainerId == tId) {
                    alreadyOverridden = TRUE;
                    fprintf(stderr, "[Mods] trainer %d overridden conflict. Winner priority kept.\n", tId);
                    break;
                }
            }
            if (alreadyOverridden) continue;

            if (sNumTrainerOverrides >= MAX_TRAINER_OVERRIDES) break;
            TrainerOverride *ov = &sTrainerOverrides[sNumTrainerOverrides++];
            ov->trainerId = tId;
            
            // Base off vanilla to keep name/class/etc if not specified
            ov->data = gTrainers[tId];
            
            // We use the most flexible party format
            ov->data.partyFlags = F_TRAINER_PARTY_CUSTOM_MOVESET | F_TRAINER_PARTY_HELD_ITEM;
            ov->data.party.ItemCustomMoves = ov->party;

            cJSON *party = cJSON_GetObjectItem(trainer, "party");
            int pSize = 0;
            if (cJSON_IsArray(party)) {
                cJSON *mon;
                cJSON_ArrayForEach(mon, party) {
                    if (pSize >= 6) break;
                    memset(&ov->party[pSize], 0, sizeof(struct TrainerMonItemCustomMoves));
                    
                    cJSON *species = cJSON_GetObjectItem(mon, "species");
                    if (cJSON_IsNumber(species)) ov->party[pSize].species = species->valueint;
                    
                    cJSON *lvl = cJSON_GetObjectItem(mon, "level");
                    if (cJSON_IsNumber(lvl)) ov->party[pSize].lvl = lvl->valueint;
                    
                    pSize++;
                }
            }
            ov->data.partySize = pSize;
            fprintf(stderr, "[Mods]   Loaded trainer override %d from %s\n", tId, mod->id);
        }
    }

    cJSON_Delete(root);
    free(jsonStr);
}


static void LoadStarterOverrides(LoadedMod *mod) {
    char path[512];
    snprintf(path, sizeof(path), "%s/data/starters.json", mod->path);
    char *jsonStr = ReadFileToString(path);
    if (!jsonStr) return;

    cJSON *root = cJSON_Parse(jsonStr);
    if (!root) {
        fprintf(stderr, "[Mods][ERROR] Invalid JSON in %s\n", path);
        free(jsonStr);
        return;
    }

    cJSON *starters = cJSON_GetObjectItem(root, "starters");
    if (cJSON_IsArray(starters)) {
        cJSON *starter;
        cJSON_ArrayForEach(starter, starters) {
            cJSON *slotObj = cJSON_GetObjectItem(starter, "slot");
            if (!cJSON_IsNumber(slotObj)) continue;
            u8 slot = slotObj->valueint;

            if (slot > 2) {
                fprintf(stderr, "[Mods][ERROR] %s: starter slot %d is out of bounds\n", mod->id, slot);
                continue;
            }

            // Check if already overridden (higher priority mod would have done it)
            bool8 alreadyOverridden = FALSE;
            for (int i = 0; i < sNumStarterOverrides; i++) {
                if (sStarterOverrides[i].slot == slot) {
                    alreadyOverridden = TRUE;
                    break;
                }
            }
            if (alreadyOverridden) continue;

            cJSON *speciesObj = cJSON_GetObjectItem(starter, "species");
            cJSON *lvlObj = cJSON_GetObjectItem(starter, "level");
            
            if (!cJSON_IsNumber(speciesObj)) {
                fprintf(stderr, "[Mods][ERROR] %s: starter slot %d has invalid species\n", mod->id, slot);
                fprintf(stderr, "[Mods] Falling back to vanilla starter for slot %d\n", slot);
                continue;
            }
            
            u16 species = speciesObj->valueint;
            if (species > NUM_SPECIES) {
                fprintf(stderr, "[Mods][ERROR] %s: starter slot %d has invalid species\n", mod->id, slot);
                fprintf(stderr, "[Mods] Falling back to vanilla starter for slot %d\n", slot);
                continue;
            }
            
            u8 level = 5;
            if (cJSON_IsNumber(lvlObj)) {
                level = lvlObj->valueint;
                if (level < 1 || level > 100) {
                    fprintf(stderr, "[Mods][ERROR] %s: starter slot %d has invalid level %d\n", mod->id, slot, level);
                    fprintf(stderr, "[Mods] Falling back to vanilla starter for slot %d\n", slot);
                    continue; // OR we could just clamp the level, but prompt says "fall back to the vanilla value"
                }
            }

            StarterOverride *ov = &sStarterOverrides[sNumStarterOverrides++];
            ov->slot = slot;
            ov->species = species;
            ov->level = level;
            
            fprintf(stderr, "[Mods]   Loaded starter override slot %d -> species %d lvl %d from %s\n", slot, species, level, mod->id);
        }
    }

    cJSON_Delete(root);
    free(jsonStr);
}

void ModManager_Init(void) {

    if (!gModsEnabled) {
        fprintf(stderr, "[Mods] Disabled\n");
        return;
    }

    fprintf(stderr, "[Mods] Mod system enabled\n");
    fprintf(stderr, "[Mods] Root: ./mods\n");

    DIR *dir = opendir("mods");
    if (!dir) {
        fprintf(stderr, "[Mods] 'mods' directory not found. No mods loaded.\n");
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char manifestPath[512];
        snprintf(manifestPath, sizeof(manifestPath), "mods/%s/mod.json", ent->d_name);

        char *jsonStr = ReadFileToString(manifestPath);
        if (!jsonStr) continue;

        cJSON *root = cJSON_Parse(jsonStr);
        if (root) {
            cJSON *id = cJSON_GetObjectItem(root, "id");
            cJSON *version = cJSON_GetObjectItem(root, "version");
            cJSON *priority = cJSON_GetObjectItem(root, "priority");

            if (cJSON_IsString(id) && cJSON_IsString(version)) {
                if (sNumLoadedMods < MAX_LOADED_MODS) {
                    LoadedMod *mod = &sLoadedMods[sNumLoadedMods++];
                    strncpy(mod->id, id->valuestring, sizeof(mod->id) - 1);
                    mod->priority = cJSON_IsNumber(priority) ? priority->valueint : 0;
                    snprintf(mod->path, sizeof(mod->path), "mods/%s", ent->d_name);
                }
            }
            cJSON_Delete(root);
        }
        free(jsonStr);
    }
    closedir(dir);

    fprintf(stderr, "[Mods] Found %d mods\n", sNumLoadedMods);

    // Sort by priority descending
    qsort(sLoadedMods, sNumLoadedMods, sizeof(LoadedMod), CompareMods);

    for (int i = 0; i < sNumLoadedMods; i++) {
        fprintf(stderr, "[Mods] Loading %s\n", sLoadedMods[i].id);
        LoadTrainerOverrides(&sLoadedMods[i]);
        LoadStarterOverrides(&sLoadedMods[i]);
    }

    fprintf(stderr, "[Mods] Loaded %d mod(s) successfully\n", sNumLoadedMods);
}

void ModManager_Shutdown(void) {
    sNumLoadedMods = 0;
    sNumTrainerOverrides = 0;
    sNumStarterOverrides = 0;
}

bool8 ModManager_IsEnabled(void) {
    return gModsEnabled;
}

const struct Trainer *ModManager_GetTrainer(u16 trainerId) {
    if (!gModsEnabled) return &gTrainers[trainerId];

    for (int i = 0; i < sNumTrainerOverrides; i++) {
        if (sTrainerOverrides[i].trainerId == trainerId) {
            return &sTrainerOverrides[i].data;
        }
    }
    return &gTrainers[trainerId];
}

bool8 ModManager_GetTrainerFrontPicOverride(u16 trainerPicId, void *destBuffer) {
    if (!gModsEnabled) return FALSE;

    for (int i = 0; i < sNumLoadedMods; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/graphics/trainer_front_pics/%d.4bpp", sLoadedMods[i].path, trainerPicId);
        
        FILE *f = fopen(path, "rb");
        if (f) {
            // Raw 64x64 4bpp is 2048 bytes
            fread(destBuffer, 1, 2048, f);
            fclose(f);
            return TRUE;
        }
    }
    return FALSE;
}


u16 ModManager_GetStarterSpecies(u8 slot, u16 vanillaSpecies) {
    if (!gModsEnabled) return vanillaSpecies;

    for (int i = 0; i < sNumStarterOverrides; i++) {
        if (sStarterOverrides[i].slot == slot) {
            return sStarterOverrides[i].species;
        }
    }
    return vanillaSpecies;
}

u8 ModManager_GetStarterLevel(u8 slot, u8 vanillaLevel) {
    if (!gModsEnabled) return vanillaLevel;

    for (int i = 0; i < sNumStarterOverrides; i++) {
        if (sStarterOverrides[i].slot == slot) {
            return sStarterOverrides[i].level;
        }
    }
    return vanillaLevel;
}

#else
bool8 gModsEnabled = FALSE;

static void LoadStarterOverrides(LoadedMod *mod) {
    char path[512];
    snprintf(path, sizeof(path), "%s/data/starters.json", mod->path);
    char *jsonStr = ReadFileToString(path);
    if (!jsonStr) return;

    cJSON *root = cJSON_Parse(jsonStr);
    if (!root) {
        fprintf(stderr, "[Mods][ERROR] Invalid JSON in %s\n", path);
        free(jsonStr);
        return;
    }

    cJSON *starters = cJSON_GetObjectItem(root, "starters");
    if (cJSON_IsArray(starters)) {
        cJSON *starter;
        cJSON_ArrayForEach(starter, starters) {
            cJSON *slotObj = cJSON_GetObjectItem(starter, "slot");
            if (!cJSON_IsNumber(slotObj)) continue;
            u8 slot = slotObj->valueint;

            if (slot > 2) {
                fprintf(stderr, "[Mods][ERROR] %s: starter slot %d is out of bounds\n", mod->id, slot);
                continue;
            }

            // Check if already overridden (higher priority mod would have done it)
            bool8 alreadyOverridden = FALSE;
            for (int i = 0; i < sNumStarterOverrides; i++) {
                if (sStarterOverrides[i].slot == slot) {
                    alreadyOverridden = TRUE;
                    break;
                }
            }
            if (alreadyOverridden) continue;

            cJSON *speciesObj = cJSON_GetObjectItem(starter, "species");
            cJSON *lvlObj = cJSON_GetObjectItem(starter, "level");
            
            if (!cJSON_IsNumber(speciesObj)) {
                fprintf(stderr, "[Mods][ERROR] %s: starter slot %d has invalid species\n", mod->id, slot);
                fprintf(stderr, "[Mods] Falling back to vanilla starter for slot %d\n", slot);
                continue;
            }
            
            u16 species = speciesObj->valueint;
            if (species > NUM_SPECIES) {
                fprintf(stderr, "[Mods][ERROR] %s: starter slot %d has invalid species\n", mod->id, slot);
                fprintf(stderr, "[Mods] Falling back to vanilla starter for slot %d\n", slot);
                continue;
            }
            
            u8 level = 5;
            if (cJSON_IsNumber(lvlObj)) {
                level = lvlObj->valueint;
                if (level < 1 || level > 100) {
                    fprintf(stderr, "[Mods][ERROR] %s: starter slot %d has invalid level %d\n", mod->id, slot, level);
                    fprintf(stderr, "[Mods] Falling back to vanilla starter for slot %d\n", slot);
                    continue; // OR we could just clamp the level, but prompt says "fall back to the vanilla value"
                }
            }

            StarterOverride *ov = &sStarterOverrides[sNumStarterOverrides++];
            ov->slot = slot;
            ov->species = species;
            ov->level = level;
            
            fprintf(stderr, "[Mods]   Loaded starter override slot %d -> species %d lvl %d from %s\n", slot, species, level, mod->id);
        }
    }

    cJSON_Delete(root);
    free(jsonStr);
}

void ModManager_Init(void) {
}
void ModManager_Shutdown(void) {}
bool8 ModManager_IsEnabled(void) { return FALSE; }
const struct Trainer *ModManager_GetTrainer(u16 trainerId) { return &gTrainers[trainerId]; }
bool8 ModManager_GetTrainerFrontPicOverride(u16 trainerPicId, void *destBuffer) { return FALSE; }
u16 ModManager_GetStarterSpecies(u8 slot, u16 vanillaSpecies) { return vanillaSpecies; }
u8 ModManager_GetStarterLevel(u8 slot, u8 vanillaLevel) { return vanillaLevel; }

#endif
