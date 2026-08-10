// Prints the struct offsets/sizes the save tool needs, straight from the
// decomp's headers, so they can never drift. Build (32-bit to match the
// game's layout):
//   gcc -m32 -DPORTABLE -DMODERN=1 -Iinclude -Igflib \
//       tools/dualscreen/print_save_offsets.c -o /tmp/print_save_offsets
#include <stdio.h>
#include <stddef.h>
#include "global.h"

int main(void)
{
    printf("{\n");
    printf("  \"sizeof_SaveBlock1\": %u,\n", (unsigned)sizeof(struct SaveBlock1));
    printf("  \"sizeof_SaveBlock2\": %u,\n", (unsigned)sizeof(struct SaveBlock2));
    printf("  \"sizeof_Pokemon\": %u,\n", (unsigned)sizeof(struct Pokemon));
    printf("  \"sb1_pos\": %u,\n", (unsigned)offsetof(struct SaveBlock1, pos));
    printf("  \"sb1_location\": %u,\n", (unsigned)offsetof(struct SaveBlock1, location));
    printf("  \"sb1_continueGameWarp\": %u,\n", (unsigned)offsetof(struct SaveBlock1, continueGameWarp));
    printf("  \"sb1_lastHealLocation\": %u,\n", (unsigned)offsetof(struct SaveBlock1, lastHealLocation));
    printf("  \"sb1_playerPartyCount\": %u,\n", (unsigned)offsetof(struct SaveBlock1, playerPartyCount));
    printf("  \"sb1_playerParty\": %u,\n", (unsigned)offsetof(struct SaveBlock1, playerParty));
    printf("  \"sb1_money\": %u,\n", (unsigned)offsetof(struct SaveBlock1, money));
    printf("  \"sb2_playerName\": %u,\n", (unsigned)offsetof(struct SaveBlock2, playerName));
    printf("  \"sb2_playTimeHours\": %u,\n", (unsigned)offsetof(struct SaveBlock2, playTimeHours));
    printf("  \"sb2_encryptionKey\": %u\n", (unsigned)offsetof(struct SaveBlock2, encryptionKey));
    printf("}\n");
    return 0;
}
