#ifndef GUARD_PLATFORM_SECONDARY_PANEL_H
#define GUARD_PLATFORM_SECONDARY_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SECONDARY_PANEL_WIDTH 240
#define SECONDARY_PANEL_HEIGHT 80
#define SECONDARY_PANEL_PARTY_SIZE 6
#define SECONDARY_PANEL_MOVE_COUNT 4
#define SECONDARY_PANEL_BAG_ROWS 4
#define SECONDARY_PANEL_MAP_RECTS 128

enum SecondaryPanelMode
{
    SECONDARY_PANEL_GATE,
    SECONDARY_PANEL_IDLE,
    SECONDARY_PANEL_PARTY,
    SECONDARY_PANEL_MAP,
    SECONDARY_PANEL_BAG,
    SECONDARY_PANEL_BATTLE_STATUS,
    SECONDARY_PANEL_BATTLE_ACTION,
    SECONDARY_PANEL_BATTLE_MOVES,
    SECONDARY_PANEL_EXIT_CONFIRMATION,
};

struct SecondaryPanelMon
{
    bool present;
    uint16_t species;
    char name[12];
    uint8_t level;
    uint16_t hp;
    uint16_t maxHp;
    uint32_t status;
    uint16_t icon[16 * 16];
};

struct SecondaryPanelMove
{
    bool present;
    uint16_t id;
    char name[14];
    uint8_t pp;
    uint8_t maxPp;
    uint8_t type;
};

struct SecondaryPanelBagRow
{
    bool present;
    uint16_t itemId;
    char name[20];
    uint16_t quantity;
    uint16_t icon[12 * 12];
};

struct SecondaryPanelMapRect
{
    uint8_t x;
    uint8_t y;
    uint8_t width;
    uint8_t height;
    bool current;
};

struct SecondaryPanelModel
{
    uint32_t revision;
    enum SecondaryPanelMode mode;
    bool inGame;
    bool inBattle;

    char message[3][36];

    struct SecondaryPanelMon party[SECONDARY_PANEL_PARTY_SIZE];
    uint8_t partyCount;
    uint8_t partySelection;

    char mapName[24];
    int16_t mapX;
    int16_t mapY;
    uint8_t mapRectCount;
    struct SecondaryPanelMapRect mapRects[SECONDARY_PANEL_MAP_RECTS];

    char pocketName[12];
    uint8_t bagSelection;
    struct SecondaryPanelBagRow bag[SECONDARY_PANEL_BAG_ROWS];

    struct SecondaryPanelMon player;
    struct SecondaryPanelMon opponent;
    struct SecondaryPanelMove moves[SECONDARY_PANEL_MOVE_COUNT];
    uint8_t battleCursor;
};

void SecondaryPanel_Reset(void);
void SecondaryPanel_Snapshot(void);
const struct SecondaryPanelModel *SecondaryPanel_GetModel(void);
void SecondaryPanel_SetSystemMode(enum SecondaryPanelMode mode,
                                  const char *line1,
                                  const char *line2,
                                  const char *line3);
void SecondaryPanel_ClearSystemMode(void);
// Step the manually selected companion view (L/R on the device). delta is +1 or
// -1; the cycle includes an "automatic" entry that restores context switching.
void SecondaryPanel_CycleView(int delta);
// Scrolls the battle command box off the top screen while its menu is open.
// Call once per frame from the platform loop, after the game thread's logic and
// immediately before running the V-blank handler.
void SecondaryPanel_HideTopBattleMenu(void);

void SecondaryPanel_Render(const struct SecondaryPanelModel *model,
                           uint16_t *pixels,
                           int pitchPixels);
void SecondaryPanel_RenderFullScreen(const struct SecondaryPanelModel *model,
                                     uint16_t *pixels,
                                     int pitchPixels,
                                     int height);

#endif
