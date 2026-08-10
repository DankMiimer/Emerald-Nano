#ifdef PLATFORM_SDL2
// Second-screen state bridge. Snapshots live game state into a JSON string
// once per frame window (throttled), for the Android bottom-screen UI or a
// desktop debug consumer. Runs on the SDL frame thread at the vblank point;
// all game reads go through the game's own accessors.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ANDROID__
#include <jni.h>
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif

#include "global.h"
#include "main.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "battle.h"
#include "battle_anim.h"
#include "data.h"
#include "item.h"
#include "event_data.h"
#include "money.h"
#include "overworld.h"
#include "region_map.h"
#include "platform/dualscreen.h"
#include "constants/battle.h"
#include "constants/characters.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/pokemon.h"
#include "constants/region_map_sections.h"

#define SNAPSHOT_CAPACITY 24576
#define SNAPSHOT_FRAME_INTERVAL 16

static char sBuffers[2][SNAPSHOT_CAPACITY];
static int sFrontBuffer;
static SDL_mutex *sSnapshotMutex;
static u32 sFrameCounter;

// ---------------------------------------------------------------------------
// GBA charset -> ASCII
// ---------------------------------------------------------------------------

static char DecodeGbaChar(u8 c)
{
    if (c == CHAR_SPACE || c == CHAR_SPACER)
        return ' ';
    if (c >= CHAR_0 && c <= CHAR_9)
        return '0' + (c - CHAR_0);
    if (c >= CHAR_A && c <= CHAR_Z)
        return 'A' + (c - CHAR_A);
    if (c >= CHAR_a && c <= CHAR_z)
        return 'a' + (c - CHAR_a);
    switch (c)
    {
    case CHAR_PERIOD:          return '.';
    case CHAR_HYPHEN:          return '-';
    case CHAR_COMMA:           return ',';
    case CHAR_SLASH:           return '/';
    case CHAR_COLON:           return ':';
    case CHAR_EXCL_MARK:       return '!';
    case CHAR_QUESTION_MARK:   return '?';
    case CHAR_SGL_QUOTE_RIGHT: return '\'';
    case CHAR_MALE:            return 'm';
    case CHAR_FEMALE:          return 'f';
    case CHAR_LEFT_PAREN:      return '(';
    case CHAR_RIGHT_PAREN:     return ')';
    case CHAR_ELLIPSIS:        return '~';
    }
    return 0; // unmapped; skipped
}

static void DecodeGbaString(char *dest, int destSize, const u8 *src, int maxSrcLen)
{
    int out = 0;
    int i;

    for (i = 0; src != NULL && i < maxSrcLen && out < destSize - 1; i++)
    {
        u8 c = src[i];
        char decoded;
        if (c == EOS)
            break;
        // Control codes consume following bytes; just stop at them.
        if (c == CHAR_NEWLINE || c == CHAR_PROMPT_SCROLL || c == CHAR_PROMPT_CLEAR
         || c == CHAR_KEYPAD_ICON || c == CHAR_EXTRA_SYMBOL
         || c == EXT_CTRL_CODE_BEGIN || c == PLACEHOLDER_BEGIN || c == CHAR_DYNAMIC)
            break;
        decoded = DecodeGbaChar(c);
        if (decoded != 0)
            dest[out++] = decoded;
    }
    dest[out] = '\0';
}

// ---------------------------------------------------------------------------
// JSON assembly
// ---------------------------------------------------------------------------

struct JsonWriter
{
    char *buffer;
    int length;
    int capacity;
};

static void JsonPut(struct JsonWriter *w, const char *format, ...)
{
    va_list args;
    int written;

    if (w->length >= w->capacity - 1)
        return;
    va_start(args, format);
    written = vsnprintf(w->buffer + w->length, w->capacity - w->length, format, args);
    va_end(args);
    if (written > 0)
    {
        w->length += written;
        if (w->length > w->capacity - 1)
            w->length = w->capacity - 1;
    }
}

// Decoded strings are plain ASCII; only quote and backslash need escaping.
static void JsonPutString(struct JsonWriter *w, const char *s)
{
    JsonPut(w, "\"");
    for (; *s != '\0'; s++)
    {
        if (*s == '"' || *s == '\\')
            JsonPut(w, "\\%c", *s);
        else
            JsonPut(w, "%c", *s);
    }
    JsonPut(w, "\"");
}

static void WritePartyMonJson(struct JsonWriter *w, struct Pokemon *mon)
{
    char text[32];
    u16 species = GetMonData(mon, MON_DATA_SPECIES_OR_EGG);
    bool32 isEgg = (GetMonData(mon, MON_DATA_IS_EGG) != 0) || species == SPECIES_EGG;
    u16 heldItem = GetMonData(mon, MON_DATA_HELD_ITEM);
    u8 ppBonuses = GetMonData(mon, MON_DATA_PP_BONUSES);
    u8 gender = GetMonGender(mon);
    u16 displaySpecies = isEgg ? GetMonData(mon, MON_DATA_SPECIES) : species;
    int i;

    JsonPut(w, "{\"species\":%u,\"dex\":%u,", displaySpecies,
            isEgg ? 0 : SpeciesToNationalPokedexNum(species));

    GetSpeciesName((u8 *)text, species);
    // GetSpeciesName writes in the GBA charset into a u8 buffer.
    {
        char decoded[24];
        DecodeGbaString(decoded, sizeof(decoded), (u8 *)text, POKEMON_NAME_LENGTH + 1);
        JsonPut(w, "\"name\":");
        JsonPutString(w, decoded);
        JsonPut(w, ",");
    }
    {
        u8 nickRaw[POKEMON_NAME_BUFFER_SIZE];
        char nick[24];
        GetMonData(mon, MON_DATA_NICKNAME, nickRaw);
        DecodeGbaString(nick, sizeof(nick), nickRaw, POKEMON_NAME_LENGTH + 1);
        JsonPut(w, "\"nick\":");
        JsonPutString(w, nick);
        JsonPut(w, ",");
    }

    JsonPut(w, "\"level\":%u,\"hp\":%u,\"maxHp\":%u,\"status\":%u,",
            GetMonData(mon, MON_DATA_LEVEL),
            GetMonData(mon, MON_DATA_HP),
            GetMonData(mon, MON_DATA_MAX_HP),
            (unsigned)GetMonData(mon, MON_DATA_STATUS));

    JsonPut(w, "\"item\":%u,\"itemName\":", heldItem);
    if (heldItem != ITEM_NONE && heldItem < ITEMS_COUNT)
    {
        char itemName[24];
        DecodeGbaString(itemName, sizeof(itemName), GetItemName(heldItem), ITEM_NAME_LENGTH);
        JsonPutString(w, itemName);
    }
    else
    {
        JsonPut(w, "\"\"");
    }

    JsonPut(w, ",\"isEgg\":%d,\"gender\":%d,", isEgg ? 1 : 0,
            gender == MON_MALE ? 0 : gender == MON_FEMALE ? 1 : 2);

    if (species < NUM_SPECIES)
        JsonPut(w, "\"types\":[%u,%u],", gSpeciesInfo[species].types[0], gSpeciesInfo[species].types[1]);
    else
        JsonPut(w, "\"types\":[0,0],");

    JsonPut(w, "\"moves\":[");
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        u16 move = GetMonData(mon, MON_DATA_MOVE1 + i);
        char moveName[24];
        if (move == MOVE_NONE || move >= MOVES_COUNT)
            continue;
        if (i > 0 && w->buffer[w->length - 1] == '}')
            JsonPut(w, ",");
        DecodeGbaString(moveName, sizeof(moveName), gMoveNames[move], MOVE_NAME_LENGTH + 1);
        JsonPut(w, "{\"id\":%u,\"name\":", move);
        JsonPutString(w, moveName);
        JsonPut(w, ",\"pp\":%u,\"maxPp\":%u,\"type\":%u}",
                GetMonData(mon, MON_DATA_PP1 + i),
                CalculatePPWithBonus(move, ppBonuses, i),
                gBattleMoves[move].type);
    }
    JsonPut(w, "]}");
}

static void WriteBattleMonJson(struct JsonWriter *w, struct BattlePokemon *mon)
{
    char text[24];
    u16 species = mon->species;
    int i;

    JsonPut(w, "{\"species\":%u,", species);
    if (species < NUM_SPECIES)
    {
        char decoded[24];
        GetSpeciesName((u8 *)text, species);
        DecodeGbaString(decoded, sizeof(decoded), (u8 *)text, POKEMON_NAME_LENGTH + 1);
        JsonPut(w, "\"name\":");
        JsonPutString(w, decoded);
        JsonPut(w, ",");
    }
    else
    {
        JsonPut(w, "\"name\":\"\",");
    }
    {
        char nick[24];
        DecodeGbaString(nick, sizeof(nick), mon->nickname, POKEMON_NAME_LENGTH + 1);
        JsonPut(w, "\"nick\":");
        JsonPutString(w, nick);
        JsonPut(w, ",");
    }
    JsonPut(w, "\"level\":%u,\"hp\":%u,\"maxHp\":%u,\"status\":%u,",
            mon->level, mon->hp, mon->maxHP, (unsigned)mon->status1);
    JsonPut(w, "\"types\":[%u,%u],", mon->types[0], mon->types[1]);
    JsonPut(w, "\"moves\":[");
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        u16 move = mon->moves[i];
        char moveName[24];
        if (move == MOVE_NONE || move >= MOVES_COUNT)
            continue;
        if (i > 0 && w->buffer[w->length - 1] == '}')
            JsonPut(w, ",");
        DecodeGbaString(moveName, sizeof(moveName), gMoveNames[move], MOVE_NAME_LENGTH + 1);
        JsonPut(w, "{\"id\":%u,\"name\":", move);
        JsonPutString(w, moveName);
        JsonPut(w, ",\"pp\":%u,\"maxPp\":%u,\"type\":%u}",
                mon->pp[i],
                CalculatePPWithBonus(move, mon->ppBonuses, i),
                gBattleMoves[move].type);
    }
    JsonPut(w, "]}");
}

static void WriteBagJson(struct JsonWriter *w)
{
    int pocket;

    JsonPut(w, "\"bag\":[");
    for (pocket = 0; pocket < POCKETS_COUNT; pocket++)
    {
        int pos;
        bool32 first = TRUE;
        if (pocket > 0)
            JsonPut(w, ",");
        JsonPut(w, "[");
        for (pos = 0; pos < gBagPockets[pocket].capacity; pos++)
        {
            u16 itemId = BagGetItemIdByPocketPosition(pocket, pos);
            u16 quantity;
            char itemName[24];
            if (itemId == ITEM_NONE || itemId >= ITEMS_COUNT)
                continue;
            quantity = BagGetQuantityByPocketPosition(pocket, pos);
            DecodeGbaString(itemName, sizeof(itemName), GetItemName(itemId), ITEM_NAME_LENGTH);
            if (!first)
                JsonPut(w, ",");
            first = FALSE;
            JsonPut(w, "{\"id\":%u,\"n\":", itemId);
            JsonPutString(w, itemName);
            JsonPut(w, ",\"q\":%u}", quantity);
        }
        JsonPut(w, "]");
    }
    JsonPut(w, "],");
}

static bool32 IsInGame(void)
{
    if (gSaveBlock1Ptr == NULL || gSaveBlock2Ptr == NULL)
        return FALSE;
    if (gPlayerPartyCount == 0 && CalculatePlayerPartyCount() == 0)
        return FALSE;
    return FlagGet(FLAG_SYS_POKEMON_GET);
}

static void BuildSnapshot(char *buffer, int capacity)
{
    struct JsonWriter writer = { buffer, 0, capacity };
    struct JsonWriter *w = &writer;
    bool32 inGame = IsInGame();
    int i;

    JsonPut(w, "{\"v\":1,\"inGame\":%d,\"inBattle\":%d,", inGame ? 1 : 0,
            (inGame && gMain.inBattle) ? 1 : 0);

    if (inGame)
    {
        char text[32];
        u8 mapsec = gMapHeader.regionMapSectionId;
        int badges = 0;

        for (i = 0; i < NUM_BADGES; i++)
        {
            if (FlagGet(FLAG_BADGE01_GET + i))
                badges++;
        }

        JsonPut(w, "\"player\":{");
        {
            char name[16];
            DecodeGbaString(name, sizeof(name), gSaveBlock2Ptr->playerName, PLAYER_NAME_LENGTH + 1);
            JsonPut(w, "\"name\":");
            JsonPutString(w, name);
            JsonPut(w, ",");
        }
        JsonPut(w, "\"gender\":%d,\"money\":%u,\"badges\":%d,",
                gSaveBlock2Ptr->playerGender, (unsigned)GetMoney(&gSaveBlock1Ptr->money), badges);
        JsonPut(w, "\"hours\":%u,\"minutes\":%u,",
                gSaveBlock2Ptr->playTimeHours, gSaveBlock2Ptr->playTimeMinutes);
        JsonPut(w, "\"mapGroup\":%d,\"mapNum\":%d,\"x\":%d,\"y\":%d,\"mapsec\":%u,",
                gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum,
                gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y, mapsec);
        if (mapsec < MAPSEC_NONE)
        {
            JsonPut(w, "\"rmx\":%u,\"rmy\":%u,\"rmw\":%u,\"rmh\":%u,",
                    gRegionMapEntries[mapsec].x, gRegionMapEntries[mapsec].y,
                    gRegionMapEntries[mapsec].width, gRegionMapEntries[mapsec].height);
        }
        {
            char mapName[24];
            GetMapName((u8 *)text, mapsec, 0);
            DecodeGbaString(mapName, sizeof(mapName), (u8 *)text, MAP_NAME_LENGTH);
            JsonPut(w, "\"mapName\":");
            JsonPutString(w, mapName);
        }
        JsonPut(w, "},");

        JsonPut(w, "\"party\":[");
        for (i = 0; i < PARTY_SIZE && i < gPlayerPartyCount; i++)
        {
            if (i > 0)
                JsonPut(w, ",");
            WritePartyMonJson(w, &gPlayerParty[i]);
        }
        JsonPut(w, "],");

        WriteBagJson(w);

        if (gMain.inBattle)
        {
            u8 playerBattler = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);
            u8 enemyBattler = GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT);
            JsonPut(w, "\"battle\":{\"kind\":%d,",
                    (gBattleTypeFlags & BATTLE_TYPE_TRAINER) ? 1 : 0);
            JsonPut(w, "\"playerMon\":");
            WriteBattleMonJson(w, &gBattleMons[playerBattler]);
            JsonPut(w, ",\"enemyMon\":");
            WriteBattleMonJson(w, &gBattleMons[enemyBattler]);
            JsonPut(w, "},");
        }
    }

    // Trim a trailing comma before closing.
    if (writer.length > 0 && buffer[writer.length - 1] == ',')
        writer.length--;
    buffer[writer.length] = '\0';
    JsonPut(w, "}");
}

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

void DualScreen_FrameHook(void)
{
    int backBuffer;

    if (sSnapshotMutex == NULL)
    {
        sSnapshotMutex = SDL_CreateMutex();
        strcpy(sBuffers[0], "{\"v\":1,\"inGame\":0,\"inBattle\":0}");
        strcpy(sBuffers[1], sBuffers[0]);
    }

    if (++sFrameCounter % SNAPSHOT_FRAME_INTERVAL != 0)
        return;

    backBuffer = sFrontBuffer ^ 1;
    BuildSnapshot(sBuffers[backBuffer], SNAPSHOT_CAPACITY);

    SDL_LockMutex(sSnapshotMutex);
    sFrontBuffer = backBuffer;
    SDL_UnlockMutex(sSnapshotMutex);

#ifndef __ANDROID__
    if (getenv("DUALSCREEN_DEBUG") != NULL && sFrameCounter % 300 == 0)
        printf("DUALSCREEN %s\n", sBuffers[sFrontBuffer]);
#endif
}

const char *DualScreen_GetSnapshotJson(void)
{
    return sBuffers[sFrontBuffer];
}

// ---------------------------------------------------------------------------
// JNI surface
// ---------------------------------------------------------------------------

#ifdef __ANDROID__

JNIEXPORT jstring JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetSnapshotJson(JNIEnv *env, jclass clazz)
{
    jstring result;
    if (sSnapshotMutex == NULL)
        return (*env)->NewStringUTF(env, "{\"v\":1,\"inGame\":0,\"inBattle\":0}");
    SDL_LockMutex(sSnapshotMutex);
    result = (*env)->NewStringUTF(env, sBuffers[sFrontBuffer]);
    SDL_UnlockMutex(sSnapshotMutex);
    return result;
}

JNIEXPORT jintArray JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetMonIcon(JNIEnv *env, jclass clazz, jint species)
{
    const u8 *tiles;
    const u16 *palette;
    jintArray result;
    jint pixels[32 * 32];
    int tileX, tileY, y, x;

    if (species <= 0 || species >= NUM_SPECIES)
        return NULL;
    tiles = GetMonIconTiles(species, TRUE);
    palette = GetValidMonIconPalettePtr(species);
    if (tiles == NULL || palette == NULL)
        return NULL;

    // First frame only: 32x32 4bpp, 4x4 tiles of 8x8, 32 bytes per tile.
    for (tileY = 0; tileY < 4; tileY++)
    for (tileX = 0; tileX < 4; tileX++)
    for (y = 0; y < 8; y++)
    for (x = 0; x < 8; x++)
    {
        int tileIndex = tileY * 4 + tileX;
        u8 packed = tiles[tileIndex * 32 + y * 4 + x / 2];
        u8 colorIndex = (x & 1) ? (packed >> 4) : (packed & 0xF);
        int px = tileX * 8 + x;
        int py = tileY * 8 + y;
        if (colorIndex == 0)
        {
            pixels[py * 32 + px] = 0;
        }
        else
        {
            u16 bgr = palette[colorIndex];
            int r = (bgr & 0x1F) << 3;
            int g = ((bgr >> 5) & 0x1F) << 3;
            int b = ((bgr >> 10) & 0x1F) << 3;
            pixels[py * 32 + px] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }

    result = (*env)->NewIntArray(env, 32 * 32);
    if (result == NULL)
        return NULL;
    (*env)->SetIntArrayRegion(env, result, 0, 32 * 32, pixels);
    return result;
}

#endif // __ANDROID__

#endif // PLATFORM_SDL2
