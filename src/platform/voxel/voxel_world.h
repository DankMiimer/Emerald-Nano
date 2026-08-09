#ifndef GUARD_VOXEL_WORLD_H
#define GUARD_VOXEL_WORLD_H

#ifdef PLATFORM_SDL2
#ifdef NATIVE_LINUX

#include <stdbool.h>

typedef enum {
    VOXEL_SHAPE_FLOOR = 0,
    VOXEL_SHAPE_WALL,
    VOXEL_SHAPE_WALL_TOP,
    VOXEL_SHAPE_LOW_BLOCK,
    VOXEL_SHAPE_MEDIUM_BLOCK,
    VOXEL_SHAPE_COUNTER,
    VOXEL_SHAPE_TABLE,
    VOXEL_SHAPE_CHAIR,
    VOXEL_SHAPE_BED,
    VOXEL_SHAPE_SHELF,
    VOXEL_SHAPE_PC,
    VOXEL_SHAPE_TV,
    VOXEL_SHAPE_VERTICAL_SPRITE,
    VOXEL_SHAPE_DECORATION,
    VOXEL_SHAPE_DOOR,
    VOXEL_SHAPE_STAIRS,
    VOXEL_SHAPE_VOID,
    VOXEL_SHAPE_COUNT
} VoxelVisualShape;

// Get metatile ID at raw map coordinates (mapX, mapY)
int VoxelWorld_GetMetatileId(int mapX, int mapY);

// Classify a map tile at raw map coordinates (mapX, mapY) into a visual shape
VoxelVisualShape VoxelWorld_ClassifyTile(int mapX, int mapY);

// Get the active map dimensions (playable area without border)
void VoxelWorld_GetMapDimensions(int *width, int *height);

// Check if map data is available
bool VoxelWorld_IsMapAvailable(void);

// Get player map coordinates
void VoxelWorld_GetPlayerCoords(int *x, int *y);

#endif // NATIVE_LINUX
#endif // PLATFORM_SDL2
#endif // GUARD_VOXEL_WORLD_H
