#include "voxel_world.h"

#ifdef PLATFORM_SDL2
#ifdef NATIVE_LINUX

#include "global.h"
#include "fieldmap.h"
#include "metatile_behavior.h"
#include "event_object_movement.h"
#include "constants/map_types.h"
#include "constants/metatile_behaviors.h"

// Removed gVoxelTerrainHeight

bool VoxelWorld_IsMapAvailable(void)
{
    extern void CB2_Overworld(void);
    extern void CB2_OverworldBasic(void);
    if (gMapHeader.mapLayout == NULL) return false;
    return (gMain.callback2 == CB2_Overworld || gMain.callback2 == CB2_OverworldBasic);
}

void VoxelWorld_GetMapDimensions(int *width, int *height)
{
    if (gMapHeader.mapLayout) {
        *width = gMapHeader.mapLayout->width;
        *height = gMapHeader.mapLayout->height;
    } else {
        *width = 0;
        *height = 0;
    }
}

void VoxelWorld_GetPlayerCoords(int *x, int *y)
{
    struct ObjectEvent *playerObj = &gObjectEvents[gPlayerAvatar.objectEventId];
    *x = playerObj->currentCoords.x - MAP_OFFSET;
    *y = playerObj->currentCoords.y - MAP_OFFSET;
}

int VoxelWorld_GetMetatileId(int mapX, int mapY)
{
    return MapGridGetMetatileIdAt(mapX + 7, mapY + 7);
}

VoxelVisualShape VoxelWorld_ClassifyTile(int mapX, int mapY)
{
    if (!gMapHeader.mapLayout) return VOXEL_SHAPE_FLOOR;

    int metatileId = VoxelWorld_GetMetatileId(mapX, mapY);
    int backupX = mapX + MAP_OFFSET;
    int backupY = mapY + MAP_OFFSET;

    u8 behavior = MapGridGetMetatileBehaviorAt(backupX, backupY);
    u8 collision = MapGridGetCollisionAt(backupX, backupY);

    if (gMapHeader.mapType == MAP_TYPE_INDOOR || gMapHeader.mapType == MAP_TYPE_SECRET_BASE) {
        if (metatileId == 622) return VOXEL_SHAPE_VOID; // Black out-of-bounds area

        // Common Furniture Behaviors
        if (behavior == MB_PC || behavior == MB_TELEVISION || metatileId == 570) return VOXEL_SHAPE_TV;
        if (behavior == MB_PICTURE_BOOK_SHELF || behavior == MB_BOOKSHELF || behavior == MB_POKEMON_CENTER_BOOKSHELF || behavior == MB_SHOP_SHELF || metatileId == 533 || metatileId == 534) return VOXEL_SHAPE_SHELF;
        if (behavior == MB_COUNTER) return VOXEL_SHAPE_COUNTER;
        if (MetatileBehavior_IsWarpDoor(behavior) || MetatileBehavior_IsDoor(behavior) || metatileId == 514 || metatileId == 515) return VOXEL_SHAPE_DOOR;
        
        // Some specific visual exceptions
        if (metatileId == 576 || metatileId == 577 || metatileId == 584 || metatileId == 585 || metatileId == 586) return VOXEL_SHAPE_TABLE;
        if (metatileId == 565 || metatileId == 558 || metatileId == 566) return VOXEL_SHAPE_CHAIR;
        if (metatileId == 578) return VOXEL_SHAPE_VERTICAL_SPRITE;
        if (metatileId == 589) return VOXEL_SHAPE_STAIRS;
        
        // Dynamic Wall Classification
        if (collision != 0) {
            u8 aboveCollision = MapGridGetCollisionAt(backupX, backupY - 1);
            if (aboveCollision != 0) {
                return VOXEL_SHAPE_WALL; // Tall wall or continuous impassable structure
            } else {
                return VOXEL_SHAPE_LOW_BLOCK; // Short obstacle (table, pot, etc)
            }
        }
        
        return VOXEL_SHAPE_FLOOR;
    }

    // OUTDOORS
    if (MetatileBehavior_IsSurfableWaterOrUnderwater(behavior)) return VOXEL_SHAPE_FLOOR;
    if (MetatileBehavior_IsTallGrass(behavior)) return VOXEL_SHAPE_FLOOR;
    if (MetatileBehavior_IsJumpSouth(behavior) || MetatileBehavior_IsJumpNorth(behavior) ||
        MetatileBehavior_IsJumpEast(behavior) || MetatileBehavior_IsJumpWest(behavior)) {
        return VOXEL_SHAPE_LOW_BLOCK;
    }
    if (MetatileBehavior_IsWarpDoor(behavior) || MetatileBehavior_IsDoor(behavior)) return VOXEL_SHAPE_DOOR;
    
    // Dynamic Wall Classification for Overworld
    if (collision != 0) {
        u8 aboveCollision = MapGridGetCollisionAt(backupX, backupY - 1);
        if (aboveCollision != 0) {
            return VOXEL_SHAPE_WALL; // Cliff, tree, building
        } else {
            return VOXEL_SHAPE_LOW_BLOCK; // Fence, rock, sign
        }
    }

    return VOXEL_SHAPE_FLOOR;
}

#endif // NATIVE_LINUX
#endif // PLATFORM_SDL2
