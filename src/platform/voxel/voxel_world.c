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

        if (metatileId >= 512) {
            switch (metatileId) {
                // Wall Tops (Row 1 and Caps)
                case 542: case 527: case 543: 
                case 560: case 561: case 562: case 563: case 564: 
                case 520: case 521: case 525: // Top of map/bookshelves
                    return VOXEL_SHAPE_WALL_TOP;
                
                // Walls (Row 2, and Row 1 for door frame)
                case 568: case 569: case 571: case 572: 
                case 528: case 529: // Map/picture lower part
                case 550: // Door frame
                case 570: // TV (Temp wall for debug)
                case 533: case 534: // Shelves (Temp wall for debug)
                    return VOXEL_SHAPE_WALL;

                // Furniture against wall (DISABLED FOR DEBUG)
                // case 570: return VOXEL_SHAPE_TV;
                // case 533: case 534: return VOXEL_SHAPE_SHELF;

                // Freestanding objects (DISABLED FOR DEBUG)
                // case 576: case 577: case 584: case 585: case 586: return VOXEL_SHAPE_TABLE;
                // case 565: case 558: case 566: return VOXEL_SHAPE_CHAIR;
                // Counters (DISABLED FOR DEBUG)
                // case 536: case 537: case 538: case 539: case 540: case 541:
                // case 544: case 545: case 546: case 547: case 548: case 549:
                // case 552: case 553: case 554: case 555: case 556: case 557:
                //    return VOXEL_SHAPE_COUNTER;

                case 578: return VOXEL_SHAPE_VERTICAL_SPRITE;

                // Architecture
                case 589: return VOXEL_SHAPE_STAIRS;
                case 514: case 515: return VOXEL_SHAPE_DOOR;
                case 513: case 517: return VOXEL_SHAPE_FLOOR;
                default: break;
            }
        }
        
        if (behavior == MB_IMPASSABLE_EAST || behavior == MB_IMPASSABLE_WEST || behavior == MB_IMPASSABLE_NORTH || behavior == MB_IMPASSABLE_SOUTH || behavior == MB_PICTURE_BOOK_SHELF || behavior == MB_BOOKSHELF || behavior == MB_POKEMON_CENTER_BOOKSHELF || behavior == MB_SHOP_SHELF || behavior == MB_REGION_MAP) {
            return VOXEL_SHAPE_WALL;
        }
        if (behavior == MB_PC || behavior == MB_TELEVISION) return VOXEL_SHAPE_PC;
        if (behavior == MB_COUNTER) return VOXEL_SHAPE_COUNTER;
        if (MetatileBehavior_IsWarpDoor(behavior) || MetatileBehavior_IsDoor(behavior)) return VOXEL_SHAPE_DOOR;
        if (collision != 0) return VOXEL_SHAPE_LOW_BLOCK;
        
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
    
    if (collision != 0) {
        if (metatileId >= 512) return VOXEL_SHAPE_WALL;
        return VOXEL_SHAPE_WALL;
    }

    return VOXEL_SHAPE_FLOOR;
}

#endif // NATIVE_LINUX
#endif // PLATFORM_SDL2
