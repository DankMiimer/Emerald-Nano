#ifdef PLATFORM_RG_NANO

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_main.h"
#include "data.h"
#include "event_data.h"
#include "item.h"
#include "item_icon.h"
#include "item_menu.h"
#include "main.h"
#include "overworld.h"
#include "party_menu.h"
#include "platform.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "region_map.h"
#include "gba/syscall.h"
#include "platform/dualscreen.h"
#include "platform/secondary_panel.h"
#include "constants/battle.h"
#include "constants/characters.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/pokemon.h"
#include "constants/region_map_sections.h"

static struct SecondaryPanelModel sModel;
static uint32_t sRevision;
static int sSystemMode = -1;
static int sViewOverride = -1; // -1 = automatic; otherwise a forced panel mode

#define BATTLE_MENU_HOLD_FRAMES 12
static int sLastBattleMenu;
static int sBattleMenuHold;
static char sSystemMessage[3][36];
static uint16_t sPartyIconCache[SECONDARY_PANEL_PARTY_SIZE][16 * 16];
static uint16_t sPartyIconSpecies[SECONDARY_PANEL_PARTY_SIZE];
static uint16_t sBagIconCache[SECONDARY_PANEL_BAG_ROWS][12 * 12];
static uint16_t sBagIconItems[SECONDARY_PANEL_BAG_ROWS];
static uint16_t sLastMapSection = MAPSEC_NONE;
static uint16_t sMapFrames;

static uint16_t Bgr555ToRgb565(uint16_t color)
{
    uint16_t red = color & 0x1F;
    uint16_t green = (color >> 5) & 0x1F;
    uint16_t blue = (color >> 10) & 0x1F;
    green = (green << 1) | (green >> 4);
    return (red << 11) | (green << 5) | blue;
}

static char DecodeGbaChar(uint8_t character)
{
    if (character == CHAR_SPACE || character == CHAR_SPACER)
        return ' ';
    if (character >= CHAR_0 && character <= CHAR_9)
        return '0' + (character - CHAR_0);
    if (character >= CHAR_A && character <= CHAR_Z)
        return 'A' + (character - CHAR_A);
    if (character >= CHAR_a && character <= CHAR_z)
        return 'a' + (character - CHAR_a);
    switch (character)
    {
    case CHAR_PERIOD: return '.';
    case CHAR_HYPHEN: return '-';
    case CHAR_COMMA: return ',';
    case CHAR_SLASH: return '/';
    case CHAR_COLON: return ':';
    case CHAR_EXCL_MARK: return '!';
    case CHAR_QUESTION_MARK: return '?';
    case CHAR_MALE: return 'M';
    case CHAR_FEMALE: return 'F';
    case CHAR_e_ACUTE: return 'E';
    default: return 0;
    }
}

static void DecodeGbaString(char *dest, size_t destSize, const uint8_t *src, int maxLength)
{
    size_t out = 0;
    int i;
    if (destSize == 0)
        return;
    for (i = 0; src != NULL && i < maxLength && out + 1 < destSize; i++)
    {
        uint8_t character = src[i];
        char decoded;
        if (character == EOS)
            break;
        if (character == CHAR_NEWLINE || character == CHAR_PROMPT_SCROLL
         || character == CHAR_PROMPT_CLEAR || character == EXT_CTRL_CODE_BEGIN
         || character == PLACEHOLDER_BEGIN || character == CHAR_DYNAMIC)
            break;
        decoded = DecodeGbaChar(character);
        if (decoded != 0)
            dest[out++] = decoded;
    }
    dest[out] = '\0';
}

static bool32 IsInGame(void)
{
    static bool32 seenGameplay;
    static uint8_t lastPlayTimeSeconds;
    static uint8_t playTimeTicks;

    if (gSaveBlock1Ptr == NULL || gSaveBlock2Ptr == NULL)
        return FALSE;
    if (gMain.callback2 == CB2_Overworld)
        seenGameplay = TRUE;
    if (gSaveBlock2Ptr->playTimeSeconds != lastPlayTimeSeconds)
    {
        lastPlayTimeSeconds = gSaveBlock2Ptr->playTimeSeconds;
        if (++playTimeTicks >= 2)
            seenGameplay = TRUE;
    }
    if (!seenGameplay || gPlayerPartyCount == 0 || gPlayerPartyCount > PARTY_SIZE)
        return FALSE;
    return FlagGet(FLAG_SYS_POKEMON_GET);
}

static void DecodeMonIcon(uint16_t species, uint16_t *dest)
{
    const uint8_t *tiles;
    const uint16_t *palette;
    int y;
    int x;
    memset(dest, 0, 16 * 16 * sizeof(*dest));
    if (species == SPECIES_NONE || species >= NUM_SPECIES)
        return;
    tiles = GetMonIconTiles(species, TRUE);
    palette = GetValidMonIconPalettePtr(species);
    if (tiles == NULL || palette == NULL)
        return;
    for (y = 0; y < 16; y++)
    {
        for (x = 0; x < 16; x++)
        {
            int sourceX = x * 2;
            int sourceY = y * 2;
            int tile = (sourceY / 8) * 4 + sourceX / 8;
            uint8_t packed = tiles[tile * 32 + (sourceY & 7) * 4 + (sourceX & 7) / 2];
            uint8_t index = (sourceX & 1) ? packed >> 4 : packed & 0xF;
            if (index != 0)
                dest[y * 16 + x] = Bgr555ToRgb565(palette[index]);
        }
    }
}

static void DecodeItemIcon(uint16_t itemId, uint16_t *dest)
{
    uint8_t gfx[0x120];
    uint16_t palette[16];
    int y;
    int x;
    memset(dest, 0, 12 * 12 * sizeof(*dest));
    if (itemId == ITEM_NONE || itemId >= ITEMS_COUNT)
        return;
    LZ77UnCompWram(GetItemIconPicOrPalette(itemId, 0), gfx);
    LZ77UnCompWram(GetItemIconPicOrPalette(itemId, 1), palette);
    for (y = 0; y < 12; y++)
    {
        for (x = 0; x < 12; x++)
        {
            int sourceX = x * 2;
            int sourceY = y * 2;
            int tile = (sourceY / 8) * 3 + sourceX / 8;
            uint8_t packed = gfx[tile * 32 + (sourceY & 7) * 4 + (sourceX & 7) / 2];
            uint8_t index = (sourceX & 1) ? packed >> 4 : packed & 0xF;
            if (index != 0)
                dest[y * 12 + x] = Bgr555ToRgb565(palette[index]);
        }
    }
}

static void FillPartyMon(struct SecondaryPanelMon *dest, struct Pokemon *mon, int slot)
{
    uint8_t nickname[POKEMON_NAME_BUFFER_SIZE];
    uint16_t species = GetMonData(mon, MON_DATA_SPECIES_OR_EGG);
    if (species == SPECIES_NONE)
        return;
    dest->present = true;
    dest->species = species;
    dest->level = GetMonData(mon, MON_DATA_LEVEL);
    dest->hp = GetMonData(mon, MON_DATA_HP);
    dest->maxHp = GetMonData(mon, MON_DATA_MAX_HP);
    dest->status = GetMonData(mon, MON_DATA_STATUS);
    GetMonData(mon, MON_DATA_NICKNAME, nickname);
    DecodeGbaString(dest->name, sizeof(dest->name), nickname, POKEMON_NAME_LENGTH + 1);
    if (sPartyIconSpecies[slot] != species)
    {
        DecodeMonIcon(species, sPartyIconCache[slot]);
        sPartyIconSpecies[slot] = species;
    }
    memcpy(dest->icon, sPartyIconCache[slot], sizeof(dest->icon));
}

static void FillBattleMon(struct SecondaryPanelMon *dest, const struct BattlePokemon *mon)
{
    dest->present = true;
    dest->species = mon->species;
    dest->level = mon->level;
    dest->hp = mon->hp;
    dest->maxHp = mon->maxHP;
    dest->status = mon->status1;
    DecodeGbaString(dest->name, sizeof(dest->name), mon->nickname, POKEMON_NAME_LENGTH + 1);
}

static void FillMap(struct SecondaryPanelModel *model, uint16_t mapSection)
{
    uint8_t mapName[32];
    int mapsec;
    model->mapX = gSaveBlock1Ptr->pos.x;
    model->mapY = gSaveBlock1Ptr->pos.y;
    GetMapName(mapName, mapSection, 0);
    DecodeGbaString(model->mapName, sizeof(model->mapName), mapName, MAP_NAME_LENGTH);
    for (mapsec = 0; mapsec < MAPSEC_NONE && model->mapRectCount < SECONDARY_PANEL_MAP_RECTS; mapsec++)
    {
        const struct RegionMapLocation *entry = &gRegionMapEntries[mapsec];
        struct SecondaryPanelMapRect *rect;
        if (entry->x > 28 || entry->y > 16)
            continue;
        rect = &model->mapRects[model->mapRectCount++];
        rect->x = entry->x;
        rect->y = entry->y;
        rect->width = entry->width;
        rect->height = entry->height;
        rect->current = mapsec == mapSection;
    }
}

static void FillBag(struct SecondaryPanelModel *model)
{
    static const char *const pocketNames[POCKETS_COUNT] = {"ITEMS", "BALLS", "TM HM", "BERRIES", "KEY ITEMS"};
    const struct ItemSlot *pockets[POCKETS_COUNT];
    int capacities[POCKETS_COUNT];
    int pocket = gBagPosition.pocket;
    int selected;
    int start;
    int row;

    pockets[ITEMS_POCKET] = gSaveBlock1Ptr->bagPocket_Items;
    capacities[ITEMS_POCKET] = BAG_ITEMS_COUNT;
    pockets[BALLS_POCKET] = gSaveBlock1Ptr->bagPocket_PokeBalls;
    capacities[BALLS_POCKET] = BAG_POKEBALLS_COUNT;
    pockets[TMHM_POCKET] = gSaveBlock1Ptr->bagPocket_TMHM;
    capacities[TMHM_POCKET] = BAG_TMHM_COUNT;
    pockets[BERRIES_POCKET] = gSaveBlock1Ptr->bagPocket_Berries;
    capacities[BERRIES_POCKET] = BAG_BERRIES_COUNT;
    pockets[KEYITEMS_POCKET] = gSaveBlock1Ptr->bagPocket_KeyItems;
    capacities[KEYITEMS_POCKET] = BAG_KEYITEMS_COUNT;

    if (pocket < 0 || pocket >= POCKETS_COUNT)
        pocket = ITEMS_POCKET;
    snprintf(model->pocketName, sizeof(model->pocketName), "%s", pocketNames[pocket]);
    selected = gBagPosition.scrollPosition[pocket] + gBagPosition.cursorPosition[pocket];
    start = selected > 1 ? selected - 1 : 0;
    if (start + SECONDARY_PANEL_BAG_ROWS > capacities[pocket])
        start = capacities[pocket] - SECONDARY_PANEL_BAG_ROWS;
    if (start < 0)
        start = 0;
    model->bagSelection = selected >= start && selected < start + SECONDARY_PANEL_BAG_ROWS
                        ? selected - start : 0xFF;

    for (row = 0; row < SECONDARY_PANEL_BAG_ROWS; row++)
    {
        int position = start + row;
        uint16_t itemId;
        struct SecondaryPanelBagRow *dest = &model->bag[row];
        if (position >= capacities[pocket])
            continue;
        itemId = pockets[pocket][position].itemId;
        if (itemId == ITEM_NONE || itemId >= ITEMS_COUNT)
            continue;
        dest->present = true;
        dest->itemId = itemId;
        dest->quantity = pockets[pocket][position].quantity ^ (uint16_t)gSaveBlock2Ptr->encryptionKey;
        DecodeGbaString(dest->name, sizeof(dest->name), GetItemName(itemId), ITEM_NAME_LENGTH);
        if (sBagIconItems[row] != itemId)
        {
            DecodeItemIcon(itemId, sBagIconCache[row]);
            sBagIconItems[row] = itemId;
        }
        memcpy(dest->icon, sBagIconCache[row], sizeof(dest->icon));
    }
}

static int ResolveBattleMenu(uint8_t *battlerOut)
{
    int32_t menuBattler;
    int menu = 0;
    uint8_t battler = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);

    // Deliberately not gated on DualScreen_BattleUiActive(). That flag turns on
    // the Android port's bottom-screen *takeover*, which also suppresses the
    // top-screen cursor and rewrites D-pad handling; this port keeps the stock
    // battle UI, so the flag stays off. These two accessors only scan
    // gBattlerControllerFuncs for the active input handler, so reading them has
    // no side effects -- the panel can mirror whichever menu is open while the
    // game keeps its own menu, cursor and controls.
    menuBattler = DualScreen_PlayerMoveBattler();
    if (menuBattler >= 0)
        menu = 2;
    else
    {
        menuBattler = DualScreen_PlayerActionBattler();
        if (menuBattler >= 0)
            menu = 1;
    }
    if (menuBattler >= 0 && menuBattler < MAX_BATTLERS_COUNT)
        battler = menuBattler;

    *battlerOut = battler;
    return menu;
}

static void FillBattle(struct SecondaryPanelModel *model)
{
    uint8_t playerBattler;
    uint8_t opponentBattler = GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT);
    int menu = ResolveBattleMenu(&playerBattler);
    int moveSlot;
    if (playerBattler >= MAX_BATTLERS_COUNT || opponentBattler >= MAX_BATTLERS_COUNT)
        return;

    // Moving between the action and move menus swaps the battler's input
    // handler, and for a few frames in between it is neither of the two we
    // recognise. Without this the panel drops to the status cards and back,
    // which reads as a flash of the "wrong" screen on every FIGHT press. Hold
    // the last real menu briefly so only genuine changes are shown.
    if (menu != 0)
    {
        sLastBattleMenu = menu;
        sBattleMenuHold = BATTLE_MENU_HOLD_FRAMES;
    }
    else if (sBattleMenuHold > 0)
    {
        sBattleMenuHold--;
        menu = sLastBattleMenu;
    }
    FillBattleMon(&model->player, &gBattleMons[playerBattler]);
    FillBattleMon(&model->opponent, &gBattleMons[opponentBattler]);
    if (menu == 1)
    {
        model->mode = SECONDARY_PANEL_BATTLE_ACTION;
        model->battleCursor = gActionSelectionCursor[playerBattler] & 3;
    }
    else if (menu == 2)
    {
        model->mode = SECONDARY_PANEL_BATTLE_MOVES;
        model->battleCursor = gMoveSelectionCursor[playerBattler] & 3;
    }
    else
    {
        model->mode = SECONDARY_PANEL_BATTLE_STATUS;
    }
    for (moveSlot = 0; moveSlot < MAX_MON_MOVES; moveSlot++)
    {
        uint16_t move = gBattleMons[playerBattler].moves[moveSlot];
        struct SecondaryPanelMove *dest = &model->moves[moveSlot];
        if (move == MOVE_NONE || move >= MOVES_COUNT)
            continue;
        dest->present = true;
        dest->id = move;
        dest->pp = gBattleMons[playerBattler].pp[moveSlot];
        dest->maxPp = CalculatePPWithBonus(move, gBattleMons[playerBattler].ppBonuses, moveSlot);
        dest->type = gBattleMoves[move].type;
        DecodeGbaString(dest->name, sizeof(dest->name), gMoveNames[move], MOVE_NAME_LENGTH + 1);
    }
}

void SecondaryPanel_Reset(void)
{
    memset(&sModel, 0, sizeof(sModel));
    memset(sPartyIconSpecies, 0, sizeof(sPartyIconSpecies));
    memset(sBagIconItems, 0, sizeof(sBagIconItems));
    sRevision = 0;
    sSystemMode = -1;
    sViewOverride = -1;
    sLastBattleMenu = 0;
    sBattleMenuHold = 0;
    sLastMapSection = MAPSEC_NONE;
    sMapFrames = 0;
    sModel.mode = SECONDARY_PANEL_IDLE;
    snprintf(sModel.message[0], sizeof(sModel.message[0]), "POKEMON EMERALD");
    snprintf(sModel.message[1], sizeof(sModel.message[1]), "RG NANO NATIVE PORT");
    snprintf(sModel.message[2], sizeof(sModel.message[2]), "240 X 160 + 240 X 80");
}

void SecondaryPanel_Snapshot(void)
{
    static struct SecondaryPanelModel next;
    bool32 inGame;
    uint16_t mapSection;
    int slot;

    memset(&next, 0, sizeof(next));
    next.partySelection = 0xFF;
    next.bagSelection = 0xFF;
    inGame = IsInGame();
    next.inGame = inGame;
    next.inBattle = inGame && gMain.inBattle;

    if (!inGame)
    {
        next.mode = SECONDARY_PANEL_IDLE;
        snprintf(next.message[0], sizeof(next.message[0]), "POKEMON EMERALD");
        snprintf(next.message[1], sizeof(next.message[1]), "RG NANO NATIVE PORT");
        snprintf(next.message[2], sizeof(next.message[2]), "PRESS START");
    }
    else
    {
        mapSection = gMapHeader.regionMapSectionId;
        if (mapSection != sLastMapSection)
        {
            sLastMapSection = mapSection;
            sMapFrames = 180;
        }
        else if (sMapFrames > 0)
        {
            sMapFrames--;
        }
        next.partyCount = gPlayerPartyCount;
        for (slot = 0; slot < PARTY_SIZE && slot < gPlayerPartyCount; slot++)
            FillPartyMon(&next.party[slot], &gPlayerParty[slot], slot);
        FillMap(&next, mapSection);

        if (next.inBattle && gBattlersCount > 0 && gBattlersCount <= MAX_BATTLERS_COUNT)
            FillBattle(&next);
        else if (sViewOverride >= 0)
            next.mode = (enum SecondaryPanelMode)sViewOverride;
        else if (gBagMenu != NULL)
        {
            next.mode = SECONDARY_PANEL_BAG;
            FillBag(&next);
        }
        else if (sMapFrames > 0)
            next.mode = SECONDARY_PANEL_MAP;
        else
            next.mode = SECONDARY_PANEL_PARTY;
    }

    if (sSystemMode >= 0)
    {
        next.mode = (enum SecondaryPanelMode)sSystemMode;
        memcpy(next.message, sSystemMessage, sizeof(next.message));
    }
    next.revision = sModel.revision;
    if (memcmp((const uint8_t *)&next + offsetof(struct SecondaryPanelModel, mode),
               (const uint8_t *)&sModel + offsetof(struct SecondaryPanelModel, mode),
               sizeof(next) - offsetof(struct SecondaryPanelModel, mode)) != 0)
        next.revision = ++sRevision;
    sModel = next;
}

const struct SecondaryPanelModel *SecondaryPanel_GetModel(void)
{
    return &sModel;
}

void SecondaryPanel_SetSystemMode(enum SecondaryPanelMode mode,
                                  const char *line1,
                                  const char *line2,
                                  const char *line3)
{
    const char *lines[3] = {line1, line2, line3};
    int line;
    sSystemMode = mode;
    for (line = 0; line < 3; line++)
        snprintf(sSystemMessage[line], sizeof(sSystemMessage[line]), "%s", lines[line] == NULL ? "" : lines[line]);
    sModel.mode = mode;
    memcpy(sModel.message, sSystemMessage, sizeof(sModel.message));
    sModel.revision = ++sRevision;
}

void SecondaryPanel_ClearSystemMode(void)
{
    sSystemMode = -1;
}

u32 DualScreen_BattleUiActive(void)
{
    if (Platform_GetSetting(PLATFORM_SETTING_BATTLE_UI_TOP))
        return FALSE;
    if (gMain.inBattle
     && (gBattleTypeFlags & (BATTLE_TYPE_SAFARI | BATTLE_TYPE_WALLY_TUTORIAL | BATTLE_TYPE_LINK)))
        return FALSE;
    return TRUE;
}

// L/R pick which companion view is shown. The overworld panel is otherwise
// entirely automatic, and Emerald itself barely uses the shoulder buttons, so
// they drive the lower screen instead. Battle always wins over a manual pick --
// the battle cards are what the player needs while a battle is on screen.
void SecondaryPanel_CycleView(int delta)
{
    static const int views[] = {
        -1,                        // automatic (map on area change, else party)
        SECONDARY_PANEL_PARTY,
        SECONDARY_PANEL_MAP,
    };
    const int count = (int)(sizeof(views) / sizeof(views[0]));
    int index = 0;
    int i;

    for (i = 0; i < count; i++)
    {
        if (views[i] == sViewOverride)
        {
            index = i;
            break;
        }
    }
    index = (index + delta % count + count) % count;
    sViewOverride = views[index];
}

u16 DualScreen_ConsumeVirtualKeys(void)
{
    return 0;
}

// The battle command box and move list live in BG0 at vertical offsets
// DISPLAY_HEIGHT and DISPLAY_HEIGHT*2; scrolling BG0 back to 0 leaves the
// battle scene with no menu over it, which is how the top box gets hidden
// while the companion panel shows the grid instead.
//
// This MUST run after the game thread's logic and immediately before the
// V-blank handler, because the battle's V-blank callback is what copies
// gBattle_BG0_Y into REG_BG0VOFS. Doing it from DualScreen_FrameHook (which
// runs *after* V-blank) let the game's own value win every other frame, so the
// menu alternated between shown and hidden -- the flashing seen on every
// keypress.
void SecondaryPanel_HideTopBattleMenu(void)
{
    if (gMain.inBattle
     && (DualScreen_PlayerAtMoveSelect() || DualScreen_PlayerAtActionSelect()))
        gBattle_BG0_Y = 0;
}

// Set from the platform layer when the companion panel is switched off, so its
// per-frame cost (this snapshot, plus the render and blit) can be measured.
bool gSecondaryPanelDisabled;

void DualScreen_FrameHook(void)
{
    if (gSecondaryPanelDisabled)
        return;
    SecondaryPanel_Snapshot();
}

const char *DualScreen_GetSnapshotJson(void)
{
    return "{}";
}

u32 DualScreen_TakeBattleTakeover(u32 mode)
{
    (void)mode;
    return FALSE;
}

void DualScreen_SetBattleMenuOpen(u32 mode, u32 caseId, u32 battler)
{
    (void)mode;
    (void)caseId;
    (void)battler;
}

void DualScreen_ClearBattleMenu(void)
{
}

u32 DualScreen_BattleMenuInfo(u32 *caseId, u32 *battler, u32 *result, u32 *seq)
{
    if (caseId != NULL) *caseId = 0;
    if (battler != NULL) *battler = 0;
    if (result != NULL) *result = 0;
    if (seq != NULL) *seq = 0;
    return 0;
}

u32 DualScreen_TakeBattleChoice(s32 *a, s32 *b)
{
    (void)a;
    (void)b;
    return FALSE;
}

void DualScreen_SetBattleMenuResult(u32 result)
{
    (void)result;
}

u32 DualScreen_BottomScreenLive(void)
{
    return FALSE;
}

#endif
