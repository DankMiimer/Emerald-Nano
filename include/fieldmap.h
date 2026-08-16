#ifndef GUARD_FIELDMAP_H
#define GUARD_FIELDMAP_H

#define NUM_TILES_IN_PRIMARY 512
#define NUM_TILES_TOTAL 1024
#define NUM_METATILES_IN_PRIMARY 512
#define NUM_METATILES_TOTAL 1024
#define NUM_PALS_IN_PRIMARY 6
#define NUM_PALS_TOTAL 13
#ifdef PLATFORM_SDL2
#define MAX_MAP_DATA_SIZE 40960
#else
#define MAX_MAP_DATA_SIZE 10240
#endif

#define NUM_TILES_PER_METATILE 8

// Map coordinates are offset by 7 when using the map
// buffer because it needs to load sufficient border
// metatiles to fill the player's view (the player has
// 7 metatiles of view horizontally in either direction).
//
// Keep MAP_OFFSET at the retail value on SDL too. This port's software PPU
// still treats sprites and collision as a 240px game space and only *reveals*
// extra BG columns via gRenderMargin. Changing MAP_OFFSET shifts every object
// coordinate and is what made the player vanish / NPCs spawn in the wrong
// place. Outdoor maps fill extra columns via GetMapFillExtraX(); indoor maps
// keep the retail 32-tile BG so warps stay aligned.
#define MAP_OFFSET 7
#define MAP_OFFSET_Y 7
#define MAP_OFFSET_W (MAP_OFFSET * 2 + 1)
#define MAP_OFFSET_H (MAP_OFFSET_Y * 2)

#ifdef PLATFORM_SDL2
// Max overworld BG size (outdoor 16:9). Indoor maps keep the retail 32-tile
// BG at runtime — see UseWideOverworldBg().
#define BG_MAP_TILES_X 64
#define MAP_FILL_EXTRA_X_WIDE 8
#else
#define BG_MAP_TILES_X 32
#define MAP_FILL_EXTRA_X_WIDE 0
#endif
#define BG_MAP_TILES_Y 32
#define OVERWORLD_BG_TILEMAP_SCREENBLOCKS (BG_MAP_TILES_X / 32)

#include "main.h"

extern struct BackupMapLayout gBackupMapLayout;

u32 MapGridGetMetatileIdAt(int x, int y);
u32 MapGridGetMetatileBehaviorAt(int x, int y);
void MapGridSetMetatileIdAt(int x, int y, u16 metatile);
void MapGridSetMetatileEntryAt(int x, int y, u16 metatile);
void GetCameraCoords(u16 *x, u16 *y);
u8 MapGridGetCollisionAt(int x, int y);
int GetMapBorderIdAt(int x, int y);
bool32 CanCameraMoveInDirection(int direction);
u16 GetMetatileAttributesById(u16 metatile);
void GetCameraFocusCoords(u16 *x, u16 *y);
u8 MapGridGetMetatileLayerTypeAt(int x, int y);
u8 MapGridGetElevationAt(int x, int y);
bool8 CameraMove(int x, int y);
void SaveMapView(void);
void SetCameraFocusCoords(u16 x, u16 y);
void InitMap(void);
void InitMapFromSavedGame(void);
void InitTrainerHillMap(void);
void InitBattlePyramidMap(bool8 setPlayerPosition);
void CopyMapTilesetsToVram(struct MapLayout const *mapLayout);
void LoadMapTilesetPalettes(struct MapLayout const *mapLayout);
void LoadSecondaryTilesetPalette(struct MapLayout const *mapLayout);
void CopySecondaryTilesetToVramUsingHeap(struct MapLayout const *mapLayout);
void CopyPrimaryTilesetToVram(struct MapLayout const *mapLayout);
void CopySecondaryTilesetToVram(struct MapLayout const *mapLayout);
const struct MapHeader *const GetMapHeaderFromConnection(const struct MapConnection *connection);
const struct MapConnection *GetMapConnectionAtPos(s16 x, s16 y);
void MapGridSetMetatileImpassabilityAt(int x, int y, bool32 impassable);
bool8 UseWideOverworldBg(void);
u32 GetBgMapTilesX(void);
u32 GetMapFillExtraX(void);
u32 MapGridGetMetatileIdForDraw(int x, int y);
u8 MapGridGetMetatileLayerTypeForDraw(int x, int y);

// field_region_map.c
void FieldInitRegionMap(MainCallback callback);

#endif //GUARD_FIELDMAP_H
