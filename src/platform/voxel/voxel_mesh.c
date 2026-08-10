#include "voxel_mesh.h"

#ifdef PLATFORM_SDL2
#ifdef NATIVE_LINUX

#include <GL/gl.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>

bool *gVoxelWallConsumed = NULL;

void VoxelMesh_BuildWalls(int mapW, int mapH)
{
    if (gVoxelWallConsumed == NULL) {
        gVoxelWallConsumed = calloc(512 * 512, sizeof(bool));
    }
    
    // Clear array up to actual map bounds
    if (gVoxelWallConsumed) {
        for (int y = 0; y < mapH; y++) {
            for (int x = 0; x < mapW; x++) {
                gVoxelWallConsumed[y * 512 + x] = false;
            }
        }
    }

    float atlasW = 512.0f;
    float atlasH = 512.0f;
    float thick = 0.1f;
    float h = 1.8f;
    float midH = h / 2.0f;

    for (int y = 0; y < mapH; y++) {
        for (int x = 0; x < mapW; x++) {
            if (gVoxelWallConsumed[y * 512 + x]) continue;

            VoxelVisualShape shape = VoxelWorld_ClassifyTile(x, y);

            if (shape == VOXEL_SHAPE_WALL) {
                // Determine facing direction
                bool faceSouth = false;
                bool faceEast = false;
                bool faceWest = false;
                
                VoxelVisualShape sS = VoxelWorld_ClassifyTile(x, y + 1);
                VoxelVisualShape sE = VoxelWorld_ClassifyTile(x + 1, y);
                VoxelVisualShape sW = VoxelWorld_ClassifyTile(x - 1, y);

                if (sS != VOXEL_SHAPE_WALL && sS != VOXEL_SHAPE_WALL_TOP && sS != VOXEL_SHAPE_VOID) faceSouth = true;
                if (sE != VOXEL_SHAPE_WALL && sE != VOXEL_SHAPE_WALL_TOP && sE != VOXEL_SHAPE_VOID) faceEast = true;
                if (sW != VOXEL_SHAPE_WALL && sW != VOXEL_SHAPE_WALL_TOP && sW != VOXEL_SHAPE_VOID) faceWest = true;
                
                if (!faceSouth && !faceEast && !faceWest) faceSouth = true;
                
                float wx = (float)x;
                float wz = (float)y;
                int lowerMeta = VoxelWorld_GetMetatileId(x, y);
                int upperMeta = VoxelWorld_GetMetatileId(x, y - 1);
                
                float u0 = (lowerMeta % 32) * 16.0f / atlasW;
                float v0 = (lowerMeta / 32) * 16.0f / atlasH;
                float u1 = u0 + (16.0f / atlasW);
                float v1 = v0 + (16.0f / atlasH);

                float t_u0 = (upperMeta % 32) * 16.0f / atlasW;
                float t_v0 = (upperMeta / 32) * 16.0f / atlasH;
                float t_u1 = t_u0 + (16.0f / atlasW);
                float t_v1 = t_v0 + (16.0f / atlasH);

                gVoxelWallConsumed[y * 512 + x] = true;
                if (y - 1 >= 0) gVoxelWallConsumed[(y - 1) * 512 + x] = true;
                if (y - 2 >= 0) {
                    VoxelVisualShape capShape = VoxelWorld_ClassifyTile(x, y - 2);
                    if (capShape == VOXEL_SHAPE_WALL_TOP || capShape == VOXEL_SHAPE_VOID) {
                        gVoxelWallConsumed[(y - 2) * 512 + x] = true;
                    }
                }

                glEnable(GL_TEXTURE_2D);
                glColor3f(1.0f, 1.0f, 1.0f);

                if (faceSouth) {
                    float zBack = wz + 1.0f - thick;
                    float zFront = wz + 1.0f;

                    glBegin(GL_QUADS);
                    // Lower Front Face
                    glTexCoord2f(u0, v1); glVertex3f(wx,      0.0f, zFront);
                    glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, 0.0f, zFront);
                    glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, midH, zFront);
                    glTexCoord2f(u0, v0); glVertex3f(wx,      midH, zFront);
                    
                    // Upper Front Face
                    glTexCoord2f(t_u0, t_v1); glVertex3f(wx,      midH, zFront);
                    glTexCoord2f(t_u1, t_v1); glVertex3f(wx+1.0f, midH, zFront);
                    glTexCoord2f(t_u1, t_v0); glVertex3f(wx+1.0f, h,    zFront);
                    glTexCoord2f(t_u0, t_v0); glVertex3f(wx,      h,    zFront);
                    glEnd();
                    
                    glDisable(GL_TEXTURE_2D);
                    glColor3f(0.2f, 0.2f, 0.2f);
                    glBegin(GL_QUADS);
                    glVertex3f(wx,      h, zBack);
                    glVertex3f(wx+1.0f, h, zBack);
                    glVertex3f(wx+1.0f, h, zFront);
                    glVertex3f(wx,      h, zFront);
                    glEnd();
                    
                    glEnable(GL_TEXTURE_2D);
                    glColor3f(1.0f, 1.0f, 1.0f);
                }
                
                if (faceEast) {
                    float xBack = wx + 1.0f - thick;
                    float xFront = wx + 1.0f;

                    glBegin(GL_QUADS);
                    // Lower Front Face (mapped with side texture)
                    glTexCoord2f(u0, v1); glVertex3f(xFront, 0.0f, wz+1.0f);
                    glTexCoord2f(u1, v1); glVertex3f(xFront, 0.0f, wz);
                    glTexCoord2f(u1, v0); glVertex3f(xFront, midH, wz);
                    glTexCoord2f(u0, v0); glVertex3f(xFront, midH, wz+1.0f);
                    
                    // Upper Front Face
                    glTexCoord2f(t_u0, t_v1); glVertex3f(xFront, midH, wz+1.0f);
                    glTexCoord2f(t_u1, t_v1); glVertex3f(xFront, midH, wz);
                    glTexCoord2f(t_u1, t_v0); glVertex3f(xFront, h,    wz);
                    glTexCoord2f(t_u0, t_v0); glVertex3f(xFront, h,    wz+1.0f);
                    glEnd();
                    
                    glDisable(GL_TEXTURE_2D);
                    glColor3f(0.2f, 0.2f, 0.2f);
                    glBegin(GL_QUADS);
                    glVertex3f(xBack,  h, wz);
                    glVertex3f(xFront, h, wz);
                    glVertex3f(xFront, h, wz+1.0f);
                    glVertex3f(xBack,  h, wz+1.0f);
                    glEnd();
                    
                    glEnable(GL_TEXTURE_2D);
                    glColor3f(1.0f, 1.0f, 1.0f);
                }
                
                if (faceWest) {
                    float xBack = wx + thick;
                    float xFront = wx;

                    glBegin(GL_QUADS);
                    // Lower Front Face (mapped with side texture)
                    glTexCoord2f(u0, v1); glVertex3f(xFront, 0.0f, wz);
                    glTexCoord2f(u1, v1); glVertex3f(xFront, 0.0f, wz+1.0f);
                    glTexCoord2f(u1, v0); glVertex3f(xFront, midH, wz+1.0f);
                    glTexCoord2f(u0, v0); glVertex3f(xFront, midH, wz);
                    
                    // Upper Front Face
                    glTexCoord2f(t_u0, t_v1); glVertex3f(xFront, midH, wz);
                    glTexCoord2f(t_u1, t_v1); glVertex3f(xFront, midH, wz+1.0f);
                    glTexCoord2f(t_u1, t_v0); glVertex3f(xFront, h,    wz+1.0f);
                    glTexCoord2f(t_u0, t_v0); glVertex3f(xFront, h,    wz);
                    glEnd();
                    
                    glDisable(GL_TEXTURE_2D);
                    glColor3f(0.2f, 0.2f, 0.2f);
                    glBegin(GL_QUADS);
                    glVertex3f(xFront, h, wz);
                    glVertex3f(xBack,  h, wz);
                    glVertex3f(xBack,  h, wz+1.0f);
                    glVertex3f(xFront, h, wz+1.0f);
                    glEnd();
                    
                    glEnable(GL_TEXTURE_2D);
                    glColor3f(1.0f, 1.0f, 1.0f);
                }
                
                glDisable(GL_TEXTURE_2D);
            }
        }
    }
}

void VoxelMesh_DrawTile(int mapX, int mapY, VoxelVisualShape shape, int metatileId)
{
    if (shape == VOXEL_SHAPE_VOID) return;
    
    // If this tile was consumed by VoxelMesh_BuildWalls (i.e. drawn as a vertical face),
    // we still want to draw it as a flat floor so there's ground underneath/behind it.
    if (gVoxelWallConsumed && gVoxelWallConsumed[mapY * 512 + mapX]) {
        shape = VOXEL_SHAPE_FLOOR;
    }

    float wx = (float)mapX;
    float wz = (float)mapY;
    
    float atlasW = 512.0f;
    float atlasH = 512.0f;
    float u0 = (metatileId % 32) * 16.0f / atlasW;
    float v0 = (metatileId / 32) * 16.0f / atlasH;
    float u1 = u0 + (16.0f / atlasW);
    float v1 = v0 + (16.0f / atlasH);

    float h = 0.0f;
    float bottomH = 0.0f;
    bool isSprite = false;
    float inset = 0.0f;

    switch (shape) {
        case VOXEL_SHAPE_FLOOR: h = 0.0f; break;
        case VOXEL_SHAPE_LOW_BLOCK: h = 0.4f; break;
        case VOXEL_SHAPE_MEDIUM_BLOCK: h = 0.8f; break;
        case VOXEL_SHAPE_COUNTER: h = 0.7f; break;
        case VOXEL_SHAPE_TABLE: h = 0.6f; bottomH = 0.5f; break;
        case VOXEL_SHAPE_CHAIR: h = 0.45f; inset = 0.2f; break;
        case VOXEL_SHAPE_BED: h = 0.4f; break;
        case VOXEL_SHAPE_SHELF: h = 1.5f; break;
        case VOXEL_SHAPE_PC: h = 0.8f; break;
        case VOXEL_SHAPE_TV: h = 1.2f; break;
        case VOXEL_SHAPE_VERTICAL_SPRITE: h = 1.0f; isSprite = true; break;
        case VOXEL_SHAPE_DECORATION: h = 0.3f; break;
        case VOXEL_SHAPE_DOOR: h = 0.05f; break;
        case VOXEL_SHAPE_STAIRS: h = 0.2f; break;
        case VOXEL_SHAPE_WALL: h = 0.0f; break; // Handled by VoxelMesh_BuildWalls
        case VOXEL_SHAPE_WALL_TOP: h = 0.0f; break;
        default: h = 1.0f; break;
    }

    static int frame_count = 0;
    if (frame_count++ < 400 && h > 0.5f) {
        printf("[VoxelGeom] local=(%d,%d) metatile=%d shape=%d height=%.2f\n", mapX, mapY, metatileId, shape, h);
    }

    if (isSprite) {
        glColor3f(1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v1); glVertex3f(wx,      0.0f, wz + 0.5f);
        glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, 0.0f, wz + 0.5f);
        glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, h,    wz + 0.5f);
        glTexCoord2f(u0, v0); glVertex3f(wx,      h,    wz + 0.5f);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        return;
    }

    bool textureOnFront = (shape == VOXEL_SHAPE_TV || shape == VOXEL_SHAPE_SHELF);
    
    // Top face
    if (textureOnFront) {
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.2f, 0.2f, 0.2f); // Dark color
    } else {
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
    }
    
    glBegin(GL_QUADS);
    if (textureOnFront) {
        glVertex3f(wx+inset,      h, wz+inset);
        glVertex3f(wx+1.0f-inset, h, wz+inset);
        glVertex3f(wx+1.0f-inset, h, wz+1.0f-inset);
        glVertex3f(wx+inset,      h, wz+1.0f-inset);
    } else {
        glTexCoord2f(u0, v0); glVertex3f(wx+inset,      h, wz+inset);
        glTexCoord2f(u1, v0); glVertex3f(wx+1.0f-inset, h, wz+inset);
        glTexCoord2f(u1, v1); glVertex3f(wx+1.0f-inset, h, wz+1.0f-inset);
        glTexCoord2f(u0, v1); glVertex3f(wx+inset,      h, wz+1.0f-inset);
    }
    glEnd();
    
    if (h > 0.0f) {
        bool cullFront = false, cullBack = false, cullLeft = false, cullRight = false;
        
        if (inset == 0.0f && bottomH == 0.0f) {
            VoxelVisualShape fShape = VoxelWorld_ClassifyTile(mapX, mapY + 1);
            VoxelVisualShape bShape = VoxelWorld_ClassifyTile(mapX, mapY - 1);
            VoxelVisualShape lShape = VoxelWorld_ClassifyTile(mapX - 1, mapY);
            VoxelVisualShape rShape = VoxelWorld_ClassifyTile(mapX + 1, mapY);
            
            if (fShape == VOXEL_SHAPE_VOID) cullFront = true;
            if (bShape == VOXEL_SHAPE_VOID) cullBack = true;
            if (lShape == VOXEL_SHAPE_VOID) cullLeft = true;
            if (rShape == VOXEL_SHAPE_VOID) cullRight = true;
            
            if (shape == fShape) cullFront = true;
            if (shape == bShape) cullBack = true;
            if (shape == lShape) cullLeft = true;
            if (shape == rShape) cullRight = true;
        }

        // Front Face
        if (!cullFront) {
            if (textureOnFront) {
                glEnable(GL_TEXTURE_2D);
                glColor3f(1.0f, 1.0f, 1.0f);
            } else {
                glDisable(GL_TEXTURE_2D);
                glColor3f(0.6f, 0.6f, 0.6f);
            }
            glBegin(GL_QUADS);
            if (textureOnFront) {
                glTexCoord2f(u0, v1); glVertex3f(wx+inset,      bottomH, wz+1.0f-inset);
                glTexCoord2f(u1, v1); glVertex3f(wx+1.0f-inset, bottomH, wz+1.0f-inset);
                glTexCoord2f(u1, v0); glVertex3f(wx+1.0f-inset, h,       wz+1.0f-inset);
                glTexCoord2f(u0, v0); glVertex3f(wx+inset,      h,       wz+1.0f-inset);
            } else {
                glVertex3f(wx+inset,      bottomH, wz+1.0f-inset);
                glVertex3f(wx+1.0f-inset, bottomH, wz+1.0f-inset);
                glVertex3f(wx+1.0f-inset, h,       wz+1.0f-inset);
                glVertex3f(wx+inset,      h,       wz+1.0f-inset);
            }
            glEnd();
        }

        glDisable(GL_TEXTURE_2D);

        // Back Face
        if (!cullBack) {
            glColor3f(0.5f, 0.5f, 0.5f);
            glBegin(GL_QUADS);
            glVertex3f(wx+1.0f-inset, bottomH, wz+inset);
            glVertex3f(wx+inset,      bottomH, wz+inset);
            glVertex3f(wx+inset,      h,       wz+inset);
            glVertex3f(wx+1.0f-inset, h,       wz+inset);
            glEnd();
        }
        
        // Left Face
        if (!cullLeft) {
            glColor3f(0.65f, 0.65f, 0.65f);
            glBegin(GL_QUADS);
            glVertex3f(wx+inset, bottomH, wz+inset);
            glVertex3f(wx+inset, bottomH, wz+1.0f-inset);
            glVertex3f(wx+inset, h,       wz+1.0f-inset);
            glVertex3f(wx+inset, h,       wz+inset);
            glEnd();
        }
        
        // Right Face
        if (!cullRight) {
            glColor3f(0.55f, 0.55f, 0.55f);
            glBegin(GL_QUADS);
            glVertex3f(wx+1.0f-inset, bottomH, wz+1.0f-inset);
            glVertex3f(wx+1.0f-inset, bottomH, wz+inset);
            glVertex3f(wx+1.0f-inset, h,       wz+inset);
            glVertex3f(wx+1.0f-inset, h,       wz+1.0f-inset);
            glEnd();
        }
    }
}

void VoxelMesh_DrawPlayerBillboard(float wx, float wy, float wz, float camX, float camY, float camZ, GLuint tex, int w, int h)
{
    (void)camX;
    (void)camY;
    (void)camZ;

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    
    if (tex != 0) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_TEXTURE_2D);
    }
    
    // Scale based on sprite width/height. 16 pixels = 1.0 world unit
    float width_world = w / 16.0f;
    float height_world = h / 16.0f;
    float hw = width_world / 2.0f;

    // Enable alpha testing so transparent pixels don't write to depth buffer
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(wx - hw, wy,     wz);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(wx + hw, wy,     wz);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(wx + hw, wy + height_world, wz);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(wx - hw, wy + height_world, wz);
    glEnd();

    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
}

#endif // NATIVE_LINUX
#endif // PLATFORM_SDL2
