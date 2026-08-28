#include "platform/secondary_panel.h"
#include "platform/rg_nano_asset_gate.h"
#include "platform/sha1.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GUARD_WORD 0xA55A

static uint64_t HashPixels(const uint16_t *pixels, size_t count)
{
    uint64_t hash = 1469598103934665603ULL;
    size_t i;
    for (i = 0; i < count; i++)
    {
        hash ^= pixels[i] & 0xFF;
        hash *= 1099511628211ULL;
        hash ^= pixels[i] >> 8;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static void Hex(char output[41], const uint8_t digest[20])
{
    static const char digits[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 20; i++)
    {
        output[i * 2] = digits[digest[i] >> 4];
        output[i * 2 + 1] = digits[digest[i] & 15];
    }
    output[40] = '\0';
}

static void TestSha1(void)
{
    static const struct
    {
        const char *input;
        const char *expected;
    } vectors[] = {
        {"", "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
        {"abc", "a9993e364706816aba3e25717850c26c9cd0d89d"},
        {"The quick brown fox jumps over the lazy dog", "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12"},
    };
    size_t i;
    for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++)
    {
        struct Sha1Context context;
        uint8_t digest[20];
        char actual[41];
        Sha1_Init(&context);
        Sha1_Update(&context, vectors[i].input, strlen(vectors[i].input));
        Sha1_Final(&context, digest);
        Hex(actual, digest);
        assert(strcmp(actual, vectors[i].expected) == 0);
    }
}

static void SeedModel(struct SecondaryPanelModel *model)
{
    int i;
    memset(model, 0, sizeof(*model));
    model->revision = 1;
    model->partyCount = 6;
    model->partySelection = 2;
    for (i = 0; i < 6; i++)
    {
        struct SecondaryPanelMon *mon = &model->party[i];
        mon->present = true;
        mon->species = i + 1;
        snprintf(mon->name, sizeof(mon->name), "MON%d", i + 1);
        mon->level = 10 + i;
        mon->hp = 10 + i * 8;
        mon->maxHp = 60;
        mon->status = i == 4;
        mon->icon[(i + 1) * 16 + i + 1] = 0xFFFF;
    }
    snprintf(model->mapName, sizeof(model->mapName), "LITTLEROOT TOWN");
    model->mapX = 12;
    model->mapY = 7;
    model->mapRectCount = 3;
    model->mapRects[0] = (struct SecondaryPanelMapRect){2, 3, 2, 1, false};
    model->mapRects[1] = (struct SecondaryPanelMapRect){7, 6, 3, 1, true};
    model->mapRects[2] = (struct SecondaryPanelMapRect){13, 9, 1, 2, false};
    snprintf(model->pocketName, sizeof(model->pocketName), "ITEMS");
    model->bagSelection = 1;
    for (i = 0; i < 4; i++)
    {
        model->bag[i].present = true;
        model->bag[i].itemId = i + 1;
        snprintf(model->bag[i].name, sizeof(model->bag[i].name), "ITEM %d", i + 1);
        model->bag[i].quantity = i * 3 + 1;
        model->bag[i].icon[i * 12 + i] = 0x07E0;
    }
    model->player = model->party[0];
    model->opponent = model->party[1];
    model->battleCursor = 2;
    for (i = 0; i < 4; i++)
    {
        model->moves[i].present = true;
        snprintf(model->moves[i].name, sizeof(model->moves[i].name), "MOVE %d", i + 1);
        model->moves[i].pp = 10 - i;
        model->moves[i].maxPp = 15;
    }
    snprintf(model->message[0], sizeof(model->message[0]), "ROM REQUIRED");
    snprintf(model->message[1], sizeof(model->message[1]), "COPY BASEROM.GBA");
    snprintf(model->message[2], sizeof(model->message[2]), "A RETRY B EXIT");
}

static void TestRenderer(void)
{
    static const enum SecondaryPanelMode modes[] = {
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
    uint16_t guarded[240 * 80 + 2];
    uint64_t hashes[sizeof(modes) / sizeof(modes[0])];
    struct SecondaryPanelModel model;
    size_t mode;
    SeedModel(&model);
    guarded[0] = guarded[240 * 80 + 1] = GUARD_WORD;
    for (mode = 0; mode < sizeof(modes) / sizeof(modes[0]); mode++)
    {
        size_t previous;
        memset(guarded + 1, 0xCC, 240 * 80 * sizeof(uint16_t));
        model.mode = modes[mode];
        SecondaryPanel_Render(&model, guarded + 1, 240);
        assert(guarded[0] == GUARD_WORD);
        assert(guarded[240 * 80 + 1] == GUARD_WORD);
        hashes[mode] = HashPixels(guarded + 1, 240 * 80);
        for (previous = 0; previous < mode; previous++)
            if (modes[previous] != SECONDARY_PANEL_IDLE || modes[mode] != SECONDARY_PANEL_GATE)
                assert(hashes[mode] != hashes[previous] || modes[mode] == SECONDARY_PANEL_EXIT_CONFIRMATION);
        printf("panel mode %d hash %016llx\n", modes[mode], (unsigned long long)hashes[mode]);
    }
}

int main(void)
{
    char detail[64];
    TestSha1();
    TestRenderer();
    assert(RgNanoAssetGate_Fill("/does/not/exist", "/does/not/exist/manifest", detail, sizeof(detail))
           == RG_NANO_ASSET_GATE_DEVELOPMENT_BUILD);
    puts("RG Nano host tests passed");
    return 0;
}
