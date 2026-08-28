#ifndef GUARD_PLATFORM_RG_NANO_ASSET_GATE_H
#define GUARD_PLATFORM_RG_NANO_ASSET_GATE_H

#include <stddef.h>

enum RgNanoAssetGateResult
{
    RG_NANO_ASSET_GATE_OK = 0,
    RG_NANO_ASSET_GATE_DEVELOPMENT_BUILD = 1,
    RG_NANO_ASSET_GATE_MANIFEST_INVALID = -1,
    RG_NANO_ASSET_GATE_ROM_MISSING = -2,
    RG_NANO_ASSET_GATE_ROM_SIZE = -3,
    RG_NANO_ASSET_GATE_ROM_HASH = -4,
    RG_NANO_ASSET_GATE_BUILD_ID = -5,
    RG_NANO_ASSET_GATE_MEMORY = -6,
};

int RgNanoAssetGate_Fill(const char *dataDirectory,
                         const char *manifestPath,
                         char *detail,
                         size_t detailSize);
const char *RgNanoAssetGate_ResultText(int result);

#endif
