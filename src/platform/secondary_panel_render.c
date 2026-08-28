#include "platform/secondary_panel.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define RGB565(r, g, b) ((uint16_t)((((r) & 0x1F) << 11) | (((g) & 0x3F) << 5) | ((b) & 0x1F)))

enum
{
    COLOR_BLACK = RGB565(0, 0, 0),
    COLOR_WHITE = RGB565(31, 63, 31),
    COLOR_TEXT = RGB565(28, 58, 27),
    COLOR_MUTED = RGB565(17, 34, 17),
    COLOR_BG = RGB565(2, 8, 7),
    COLOR_CARD = RGB565(4, 14, 12),
    COLOR_CARD_ALT = RGB565(5, 19, 15),
    COLOR_BORDER = RGB565(8, 29, 22),
    COLOR_ACCENT = RGB565(6, 48, 20),
    COLOR_ACCENT_LIGHT = RGB565(15, 61, 24),
    COLOR_YELLOW = RGB565(31, 53, 3),
    COLOR_RED = RGB565(28, 8, 5),
    COLOR_ORANGE = RGB565(31, 30, 3),
    COLOR_BLUE = RGB565(5, 26, 27),
};

static const uint8_t *GetGlyph(char character)
{
    static const uint8_t glyphs[][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
        {15,16,16,16,16,16,15}, {30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {15,16,16,19,17,17,15}, {17,17,17,31,17,17,17},
        {31,4,4,4,4,4,31},      {7,2,2,2,18,18,12},
        {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},   {31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},     {31,1,2,4,8,16,31},
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31},     {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2},     {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
    };
    static const uint8_t hyphen[7] = {0,0,0,31,0,0,0};
    static const uint8_t dot[7] = {0,0,0,0,0,12,12};
    static const uint8_t colon[7] = {0,12,12,0,12,12,0};
    static const uint8_t slash[7] = {1,2,2,4,8,8,16};
    static const uint8_t question[7] = {14,17,1,2,4,0,4};
    static const uint8_t exclamation[7] = {4,4,4,4,4,0,4};
    static const uint8_t percent[7] = {25,25,2,4,8,19,19};
    static const uint8_t plus[7] = {0,4,4,31,4,4,0};

    character = (char)toupper((unsigned char)character);
    if (character >= 'A' && character <= 'Z')
        return glyphs[character - 'A'];
    if (character >= '0' && character <= '9')
        return glyphs[26 + character - '0'];
    switch (character)
    {
    case '-': return hyphen;
    case '.': return dot;
    case ':': return colon;
    case '/': return slash;
    case '?': return question;
    case '!': return exclamation;
    case '%': return percent;
    case '+': return plus;
    default: return NULL;
    }
}

static void Fill(uint16_t *pixels, int pitch, int x, int y, int width, int height, uint16_t color)
{
    int row;
    int col;
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > SECONDARY_PANEL_WIDTH) width = SECONDARY_PANEL_WIDTH - x;
    if (width <= 0 || height <= 0)
        return;
    for (row = 0; row < height; row++)
        for (col = 0; col < width; col++)
            pixels[(y + row) * pitch + x + col] = color;
}

static void Outline(uint16_t *pixels, int pitch, int x, int y, int width, int height, uint16_t color)
{
    Fill(pixels, pitch, x, y, width, 1, color);
    Fill(pixels, pitch, x, y + height - 1, width, 1, color);
    Fill(pixels, pitch, x, y, 1, height, color);
    Fill(pixels, pitch, x + width - 1, y, 1, height, color);
}

static int TextWidth(const char *text, int scale)
{
    return text == NULL ? 0 : (int)strlen(text) * 6 * scale - scale;
}

static void Text(uint16_t *pixels, int pitch, int x, int y, const char *text,
                 uint16_t color, int scale, int maxChars)
{
    int character;
    if (text == NULL || scale < 1)
        return;
    for (character = 0; text[character] != '\0' && (maxChars < 0 || character < maxChars); character++)
    {
        const uint8_t *glyph = GetGlyph(text[character]);
        int row;
        int column;
        if (glyph == NULL)
            continue;
        for (row = 0; row < 7; row++)
            for (column = 0; column < 5; column++)
                if (glyph[row] & (1 << (4 - column)))
                    Fill(pixels, pitch, x + (character * 6 + column) * scale,
                         y + row * scale, scale, scale, color);
    }
}

static void CenterText(uint16_t *pixels, int pitch, int x, int y, int width,
                       const char *text, uint16_t color, int scale, int maxChars)
{
    int measured = TextWidth(text, scale);
    if (maxChars >= 0 && (int)strlen(text) > maxChars)
        measured = maxChars * 6 * scale - scale;
    Text(pixels, pitch, x + (width - measured) / 2, y, text, color, scale, maxChars);
}

static void DrawIcon(uint16_t *pixels, int pitch, int x, int y,
                     const uint16_t *icon, int width, int height)
{
    int px;
    int py;
    for (py = 0; py < height; py++)
        for (px = 0; px < width; px++)
            if (icon[py * width + px] != 0)
                pixels[(y + py) * pitch + x + px] = icon[py * width + px];
}

static uint16_t HpColor(uint16_t hp, uint16_t maxHp)
{
    if (maxHp == 0 || hp * 5 <= maxHp)
        return COLOR_RED;
    if (hp * 2 <= maxHp)
        return COLOR_YELLOW;
    return COLOR_ACCENT_LIGHT;
}

static void DrawParty(uint16_t *pixels, int pitch, const struct SecondaryPanelModel *model)
{
    int i;
    Fill(pixels, pitch, 0, 0, 240, 80, COLOR_BG);
    for (i = 0; i < SECONDARY_PANEL_PARTY_SIZE; i++)
    {
        const struct SecondaryPanelMon *mon = &model->party[i];
        int x = (i % 3) * 80;
        int y = (i / 3) * 40;
        char level[8];
        int hpWidth;
        Fill(pixels, pitch, x + 1, y + 1, 78, 38, (i & 1) ? COLOR_CARD_ALT : COLOR_CARD);
        Outline(pixels, pitch, x, y, 80, 40,
                model->partySelection == i ? COLOR_YELLOW : COLOR_BORDER);
        if (!mon->present)
        {
            CenterText(pixels, pitch, x, y + 16, 80, "EMPTY", COLOR_MUTED, 1, -1);
            continue;
        }
        DrawIcon(pixels, pitch, x + 2, y + 4, mon->icon, 16, 16);
        Text(pixels, pitch, x + 20, y + 3, mon->name, COLOR_TEXT, 1, 9);
        snprintf(level, sizeof(level), "L%u", (unsigned)mon->level);
        Text(pixels, pitch, x + 20, y + 13, level, COLOR_MUTED, 1, -1);
        Fill(pixels, pitch, x + 20, y + 25, 55, 5, COLOR_BLACK);
        hpWidth = mon->maxHp == 0 ? 0 : (int)mon->hp * 53 / mon->maxHp;
        if (hpWidth > 53) hpWidth = 53;
        Fill(pixels, pitch, x + 21, y + 26, hpWidth, 3, HpColor(mon->hp, mon->maxHp));
        if (mon->status != 0)
            Text(pixels, pitch, x + 2, y + 30, "STS", COLOR_ORANGE, 1, -1);
    }
}

static void DrawMap(uint16_t *pixels, int pitch, const struct SecondaryPanelModel *model)
{
    int i;
    Fill(pixels, pitch, 0, 0, 240, 80, COLOR_BG);
    Fill(pixels, pitch, 1, 1, 118, 78, COLOR_BLUE);
    for (i = 0; i < model->mapRectCount && i < SECONDARY_PANEL_MAP_RECTS; i++)
    {
        const struct SecondaryPanelMapRect *rect = &model->mapRects[i];
        int x = 4 + rect->x * 4;
        int y = 7 + rect->y * 4;
        int w = rect->width == 0 ? 3 : rect->width * 4;
        int h = rect->height == 0 ? 3 : rect->height * 4;
        if (x < 119 && y < 79)
            Fill(pixels, pitch, x, y, w, h, rect->current ? COLOR_YELLOW : COLOR_ACCENT);
    }
    Outline(pixels, pitch, 0, 0, 120, 80, COLOR_BORDER);
    Text(pixels, pitch, 126, 8, "LOCATION", COLOR_MUTED, 1, -1);
    Text(pixels, pitch, 126, 21, model->mapName, COLOR_TEXT, 1, 18);
    {
        char coordinates[24];
        snprintf(coordinates, sizeof(coordinates), "X:%d Y:%d", model->mapX, model->mapY);
        Text(pixels, pitch, 126, 36, coordinates, COLOR_MUTED, 1, -1);
    }
    Text(pixels, pitch, 126, 61, "HOENN", COLOR_ACCENT_LIGHT, 1, -1);
}

static void DrawBag(uint16_t *pixels, int pitch, const struct SecondaryPanelModel *model)
{
    int i;
    Fill(pixels, pitch, 0, 0, 240, 80, COLOR_BG);
    Fill(pixels, pitch, 0, 0, 240, 12, COLOR_CARD_ALT);
    Text(pixels, pitch, 5, 2, model->pocketName, COLOR_ACCENT_LIGHT, 1, -1);
    for (i = 0; i < SECONDARY_PANEL_BAG_ROWS; i++)
    {
        const struct SecondaryPanelBagRow *row = &model->bag[i];
        int y = 13 + i * 16;
        char quantity[10];
        if (model->bagSelection == i)
        {
            Fill(pixels, pitch, 1, y, 238, 15, COLOR_CARD_ALT);
            Outline(pixels, pitch, 0, y - 1, 240, 17, COLOR_YELLOW);
        }
        if (!row->present)
            continue;
        DrawIcon(pixels, pitch, 3, y + 1, row->icon, 12, 12);
        Text(pixels, pitch, 19, y + 3, row->name, COLOR_TEXT, 1, 26);
        snprintf(quantity, sizeof(quantity), "X%u", (unsigned)row->quantity);
        Text(pixels, pitch, 207, y + 3, quantity, COLOR_MUTED, 1, 5);
    }
}

static void DrawBattleCard(uint16_t *pixels, int pitch, int x, const char *label,
                           const struct SecondaryPanelMon *mon)
{
    char level[8];
    int hpWidth;
    Fill(pixels, pitch, x + 1, 1, 118, 78, COLOR_CARD);
    Outline(pixels, pitch, x, 0, 120, 80, COLOR_BORDER);
    Text(pixels, pitch, x + 5, 5, label, COLOR_MUTED, 1, -1);
    Text(pixels, pitch, x + 5, 18, mon->name, COLOR_TEXT, 1, 16);
    snprintf(level, sizeof(level), "L%u", (unsigned)mon->level);
    Text(pixels, pitch, x + 5, 31, level, COLOR_MUTED, 1, -1);
    Fill(pixels, pitch, x + 5, 48, 108, 8, COLOR_BLACK);
    hpWidth = mon->maxHp == 0 ? 0 : (int)mon->hp * 106 / mon->maxHp;
    if (hpWidth > 106) hpWidth = 106;
    Fill(pixels, pitch, x + 6, 49, hpWidth, 6, HpColor(mon->hp, mon->maxHp));
    {
        char hp[20];
        snprintf(hp, sizeof(hp), "%u/%u", (unsigned)mon->hp, (unsigned)mon->maxHp);
        Text(pixels, pitch, x + 5, 63, hp, COLOR_MUTED, 1, -1);
    }
}

static void DrawBattle(uint16_t *pixels, int pitch, const struct SecondaryPanelModel *model)
{
    static const char *const actions[4] = {"FIGHT", "BAG", "PKMN", "RUN"};
    int i;
    Fill(pixels, pitch, 0, 0, 240, 80, COLOR_BG);
    if (model->mode == SECONDARY_PANEL_BATTLE_STATUS)
    {
        DrawBattleCard(pixels, pitch, 0, "YOUR PKMN", &model->player);
        DrawBattleCard(pixels, pitch, 120, "OPPONENT", &model->opponent);
        return;
    }
    for (i = 0; i < 4; i++)
    {
        int x = (i & 1) * 120;
        int y = (i >> 1) * 40;
        bool selected = model->battleCursor == i;
        Fill(pixels, pitch, x + 1, y + 1, 118, 38, selected ? COLOR_CARD_ALT : COLOR_CARD);
        Outline(pixels, pitch, x, y, 120, 40, selected ? COLOR_YELLOW : COLOR_BORDER);
        if (model->mode == SECONDARY_PANEL_BATTLE_ACTION)
        {
            CenterText(pixels, pitch, x, y + 16, 120, actions[i],
                       selected ? COLOR_WHITE : COLOR_TEXT, 1, -1);
        }
        else if (model->moves[i].present)
        {
            char pp[12];
            Text(pixels, pitch, x + 5, y + 7, model->moves[i].name,
                 selected ? COLOR_WHITE : COLOR_TEXT, 1, 15);
            snprintf(pp, sizeof(pp), "PP %u/%u", (unsigned)model->moves[i].pp,
                     (unsigned)model->moves[i].maxPp);
            Text(pixels, pitch, x + 5, y + 23, pp, COLOR_MUTED, 1, -1);
        }
        else
        {
            CenterText(pixels, pitch, x, y + 16, 120, "--", COLOR_MUTED, 1, -1);
        }
    }
}

static void DrawMessage(uint16_t *pixels, int pitch, const struct SecondaryPanelModel *model)
{
    Fill(pixels, pitch, 0, 0, 240, 80, COLOR_BG);
    Outline(pixels, pitch, 0, 0, 240, 80, model->mode == SECONDARY_PANEL_GATE ? COLOR_ACCENT_LIGHT : COLOR_YELLOW);
    CenterText(pixels, pitch, 4, 10, 232, model->message[0], COLOR_WHITE, 1, 36);
    CenterText(pixels, pitch, 4, 31, 232, model->message[1], COLOR_TEXT, 1, 36);
    CenterText(pixels, pitch, 4, 52, 232, model->message[2], COLOR_MUTED, 1, 36);
}

void SecondaryPanel_Render(const struct SecondaryPanelModel *model,
                           uint16_t *pixels,
                           int pitchPixels)
{
    if (model == NULL || pixels == NULL || pitchPixels < SECONDARY_PANEL_WIDTH)
        return;
    switch (model->mode)
    {
    case SECONDARY_PANEL_PARTY: DrawParty(pixels, pitchPixels, model); break;
    case SECONDARY_PANEL_MAP: DrawMap(pixels, pitchPixels, model); break;
    case SECONDARY_PANEL_BAG: DrawBag(pixels, pitchPixels, model); break;
    case SECONDARY_PANEL_BATTLE_STATUS:
    case SECONDARY_PANEL_BATTLE_ACTION:
    case SECONDARY_PANEL_BATTLE_MOVES:
        DrawBattle(pixels, pitchPixels, model);
        break;
    case SECONDARY_PANEL_GATE:
    case SECONDARY_PANEL_EXIT_CONFIRMATION:
    case SECONDARY_PANEL_IDLE:
    default:
        DrawMessage(pixels, pitchPixels, model);
        break;
    }
}

void SecondaryPanel_RenderFullScreen(const struct SecondaryPanelModel *model,
                                     uint16_t *pixels,
                                     int pitchPixels,
                                     int height)
{
    int y;
    if (pixels == NULL || pitchPixels < SECONDARY_PANEL_WIDTH || height < SECONDARY_PANEL_HEIGHT)
        return;
    for (y = 0; y < height; y++)
        Fill(pixels, pitchPixels, 0, y, SECONDARY_PANEL_WIDTH, 1, COLOR_BG);
    Outline(pixels, pitchPixels, 8, 8, 224, height - 16, COLOR_ACCENT_LIGHT);
    CenterText(pixels, pitchPixels, 12, 34, 216, "POKEMON EMERALD NANO", COLOR_ACCENT_LIGHT, 2, 20);
    // Yellow: this line is the failure reason, and it should read as a warning
    // rather than as ordinary body text.
    CenterText(pixels, pitchPixels, 12, 92, 216, model->message[0], COLOR_YELLOW, 1, 36);
    CenterText(pixels, pitchPixels, 12, 119, 216, model->message[1], COLOR_TEXT, 1, 36);
    CenterText(pixels, pitchPixels, 12, 146, 216, model->message[2], COLOR_MUTED, 1, 36);
    CenterText(pixels, pitchPixels, 12, height - 35, 216, "A RETRY   B EXIT", COLOR_YELLOW, 1, -1);
}
