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
                    glVertex3f(wx,      0.0f, wz);
                    glVertex3f(wx+1.0f, 0.0f, wz);
                    glVertex3f(wx+1.0f, 0.0f, wz+1.0f);
                    glVertex3f(wx,      0.0f, wz+1.0f);
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

static void VoxelMesh_DrawFurniture(int mapX, int mapY, VoxelVisualShape shape, float u0, float v0, float u1, float v1)
{
    float wx = (float)mapX;
    float wz = (float)mapY;
    
    // Check neighbors for multi-tile furniture connection
    bool n_is_same = VoxelWorld_ClassifyTile(mapX, mapY - 1) == shape;
    bool s_is_same = VoxelWorld_ClassifyTile(mapX, mapY + 1) == shape;
    bool e_is_same = VoxelWorld_ClassifyTile(mapX + 1, mapY) == shape;
    bool w_is_same = VoxelWorld_ClassifyTile(mapX - 1, mapY) == shape;

    if (shape == VOXEL_SHAPE_FURNITURE_TABLE) {
        float tHeight = 0.5f;
        float tThick = 0.1f;
        
        // Table top
        glColor3f(1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0); glVertex3f(wx,      tHeight, wz);
        glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, tHeight, wz);
        glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, tHeight, wz+1.0f);
        glTexCoord2f(u0, v1); glVertex3f(wx,      tHeight, wz+1.0f);
        glEnd();
        
        // Table top sides
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.4f, 0.3f, 0.2f); // Dark wood color
        glBegin(GL_QUADS);
        if (!n_is_same) {
            glVertex3f(wx+1.0f, tHeight-tThick, wz); glVertex3f(wx,      tHeight-tThick, wz);
            glVertex3f(wx,      tHeight,        wz); glVertex3f(wx+1.0f, tHeight,        wz);
        }
        if (!s_is_same) {
            glVertex3f(wx,      tHeight-tThick, wz+1.0f); glVertex3f(wx+1.0f, tHeight-tThick, wz+1.0f);
            glVertex3f(wx+1.0f, tHeight,        wz+1.0f); glVertex3f(wx,      tHeight,        wz+1.0f);
        }
        if (!e_is_same) {
            glVertex3f(wx+1.0f, tHeight-tThick, wz+1.0f); glVertex3f(wx+1.0f, tHeight-tThick, wz);
            glVertex3f(wx+1.0f, tHeight,        wz);      glVertex3f(wx+1.0f, tHeight,        wz+1.0f);
        }
        if (!w_is_same) {
            glVertex3f(wx,      tHeight-tThick, wz);      glVertex3f(wx,      tHeight-tThick, wz+1.0f);
            glVertex3f(wx,      tHeight,        wz+1.0f); glVertex3f(wx,      tHeight,        wz);
        }
        glEnd();
        
        // Legs (only on absolute corners of the connected structure)
        glColor3f(0.3f, 0.2f, 0.15f);
        float lW = 0.1f; // leg width
        glBegin(GL_QUADS);
        // Top-left leg
        if (!n_is_same && !w_is_same) {
            glVertex3f(wx,      0.0f, wz); glVertex3f(wx+lW,   0.0f, wz);
            glVertex3f(wx+lW,   tHeight-tThick, wz); glVertex3f(wx,      tHeight-tThick, wz);
            glVertex3f(wx,      0.0f, wz+lW); glVertex3f(wx+lW,   0.0f, wz+lW);
            glVertex3f(wx+lW,   tHeight-tThick, wz+lW); glVertex3f(wx,      tHeight-tThick, wz+lW);
            glVertex3f(wx+lW,   0.0f, wz); glVertex3f(wx+lW,   0.0f, wz+lW);
            glVertex3f(wx+lW,   tHeight-tThick, wz+lW); glVertex3f(wx+lW,   tHeight-tThick, wz);
        }
        // Top-right leg
        if (!n_is_same && !e_is_same) {
            glVertex3f(wx+1.0f-lW, 0.0f, wz); glVertex3f(wx+1.0f,    0.0f, wz);
            glVertex3f(wx+1.0f,    tHeight-tThick, wz); glVertex3f(wx+1.0f-lW, tHeight-tThick, wz);
            glVertex3f(wx+1.0f-lW, 0.0f, wz+lW); glVertex3f(wx+1.0f,    0.0f, wz+lW);
            glVertex3f(wx+1.0f,    tHeight-tThick, wz+lW); glVertex3f(wx+1.0f-lW, tHeight-tThick, wz+lW);
            glVertex3f(wx+1.0f-lW, 0.0f, wz); glVertex3f(wx+1.0f-lW, 0.0f, wz+lW);
            glVertex3f(wx+1.0f-lW, tHeight-tThick, wz+lW); glVertex3f(wx+1.0f-lW, tHeight-tThick, wz);
        }
        // Bottom-left leg
        if (!s_is_same && !w_is_same) {
            glVertex3f(wx,      0.0f, wz+1.0f-lW); glVertex3f(wx+lW,   0.0f, wz+1.0f-lW);
            glVertex3f(wx+lW,   tHeight-tThick, wz+1.0f-lW); glVertex3f(wx,      tHeight-tThick, wz+1.0f-lW);
            glVertex3f(wx,      0.0f, wz+1.0f); glVertex3f(wx+lW,   0.0f, wz+1.0f);
            glVertex3f(wx+lW,   tHeight-tThick, wz+1.0f); glVertex3f(wx,      tHeight-tThick, wz+1.0f);
            glVertex3f(wx+lW,   0.0f, wz+1.0f-lW); glVertex3f(wx+lW,   0.0f, wz+1.0f);
            glVertex3f(wx+lW,   tHeight-tThick, wz+1.0f); glVertex3f(wx+lW,   tHeight-tThick, wz+1.0f-lW);
        }
        // Bottom-right leg
        if (!s_is_same && !e_is_same) {
            glVertex3f(wx+1.0f-lW, 0.0f, wz+1.0f-lW); glVertex3f(wx+1.0f,    0.0f, wz+1.0f-lW);
            glVertex3f(wx+1.0f,    tHeight-tThick, wz+1.0f-lW); glVertex3f(wx+1.0f-lW, tHeight-tThick, wz+1.0f-lW);
            glVertex3f(wx+1.0f-lW, 0.0f, wz+1.0f); glVertex3f(wx+1.0f,    0.0f, wz+1.0f);
            glVertex3f(wx+1.0f,    tHeight-tThick, wz+1.0f); glVertex3f(wx+1.0f-lW, tHeight-tThick, wz+1.0f);
            glVertex3f(wx+1.0f-lW, 0.0f, wz+1.0f-lW); glVertex3f(wx+1.0f-lW, 0.0f, wz+1.0f);
            glVertex3f(wx+1.0f-lW, tHeight-tThick, wz+1.0f); glVertex3f(wx+1.0f-lW, tHeight-tThick, wz+1.0f-lW);
        }
        glEnd();
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
    }
    else if (shape == VOXEL_SHAPE_FURNITURE_TV) {
        float bHeight = 0.3f; // base height
        float tHeight = 1.0f; // tv height
        
        // Base / Stand
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.2f, 0.2f, 0.2f);
        glBegin(GL_QUADS);
        // Base top
        glVertex3f(wx,      bHeight, wz);
        glVertex3f(wx+1.0f, bHeight, wz);
        glVertex3f(wx+1.0f, bHeight, wz+1.0f);
        glVertex3f(wx,      bHeight, wz+1.0f);
        // Base front
        glVertex3f(wx,      0.0f, wz+1.0f); glVertex3f(wx+1.0f, 0.0f, wz+1.0f);
        glVertex3f(wx+1.0f, bHeight, wz+1.0f); glVertex3f(wx,      bHeight, wz+1.0f);
        // Base sides
        if (!w_is_same) {
            glVertex3f(wx, 0.0f, wz); glVertex3f(wx, 0.0f, wz+1.0f);
            glVertex3f(wx, bHeight, wz+1.0f); glVertex3f(wx, bHeight, wz);
        }
        if (!e_is_same) {
            glVertex3f(wx+1.0f, 0.0f, wz+1.0f); glVertex3f(wx+1.0f, 0.0f, wz);
            glVertex3f(wx+1.0f, bHeight, wz); glVertex3f(wx+1.0f, bHeight, wz+1.0f);
        }
        glEnd();
        
        // TV Screen (textured front)
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v1); glVertex3f(wx,      bHeight, wz+0.8f);
        glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, bHeight, wz+0.8f);
        glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, tHeight, wz+0.8f);
        glTexCoord2f(u0, v0); glVertex3f(wx,      tHeight, wz+0.8f);
        glEnd();
        
        // TV Body (solid)
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.1f, 0.1f, 0.1f);
        glBegin(GL_QUADS);
        // Top
        glVertex3f(wx,      tHeight, wz+0.3f);
        glVertex3f(wx+1.0f, tHeight, wz+0.3f);
        glVertex3f(wx+1.0f, tHeight, wz+0.8f);
        glVertex3f(wx,      tHeight, wz+0.8f);
        // Sides
        if (!w_is_same) {
            glVertex3f(wx, bHeight, wz+0.3f); glVertex3f(wx, bHeight, wz+0.8f);
            glVertex3f(wx, tHeight, wz+0.8f); glVertex3f(wx, tHeight, wz+0.3f);
        }
        if (!e_is_same) {
            glVertex3f(wx+1.0f, bHeight, wz+0.8f); glVertex3f(wx+1.0f, bHeight, wz+0.3f);
            glVertex3f(wx+1.0f, tHeight, wz+0.3f); glVertex3f(wx+1.0f, tHeight, wz+0.8f);
        }
        glEnd();
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
    }
    else if (shape == VOXEL_SHAPE_FURNITURE_SHELF) {
        float sHeight = 1.6f;
        // Textured front
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v1); glVertex3f(wx,      0.0f, wz+1.0f);
        glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, 0.0f, wz+1.0f);
        glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, sHeight, wz+1.0f);
        glTexCoord2f(u0, v0); glVertex3f(wx,      sHeight, wz+1.0f);
        glEnd();
        
        // Solid body
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.3f, 0.2f, 0.1f);
        glBegin(GL_QUADS);
        // Top
        glVertex3f(wx,      sHeight, wz);
        glVertex3f(wx+1.0f, sHeight, wz);
        glVertex3f(wx+1.0f, sHeight, wz+1.0f);
        glVertex3f(wx,      sHeight, wz+1.0f);
        // Sides
        if (!w_is_same) {
            glVertex3f(wx, 0.0f, wz); glVertex3f(wx, 0.0f, wz+1.0f);
            glVertex3f(wx, sHeight, wz+1.0f); glVertex3f(wx, sHeight, wz);
        }
        if (!e_is_same) {
            glVertex3f(wx+1.0f, 0.0f, wz+1.0f); glVertex3f(wx+1.0f, 0.0f, wz);
            glVertex3f(wx+1.0f, sHeight, wz); glVertex3f(wx+1.0f, sHeight, wz+1.0f);
        }
        glEnd();
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
    }
    else if (shape == VOXEL_SHAPE_FURNITURE_CHAIR) {
        float sHeight = 0.4f;
        // Seat top (textured)
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0); glVertex3f(wx+0.1f, sHeight, wz+0.1f);
        glTexCoord2f(u1, v0); glVertex3f(wx+0.9f, sHeight, wz+0.1f);
        glTexCoord2f(u1, v1); glVertex3f(wx+0.9f, sHeight, wz+0.9f);
        glTexCoord2f(u0, v1); glVertex3f(wx+0.1f, sHeight, wz+0.9f);
        glEnd();
        
        // Base block
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.4f, 0.4f, 0.4f);
        glBegin(GL_QUADS);
        // Front
        glVertex3f(wx+0.2f, 0.0f, wz+0.8f); glVertex3f(wx+0.8f, 0.0f, wz+0.8f);
        glVertex3f(wx+0.8f, sHeight, wz+0.8f); glVertex3f(wx+0.2f, sHeight, wz+0.8f);
        // Backrest (solid tall part on North side)
        glVertex3f(wx+0.1f, sHeight, wz+0.2f); glVertex3f(wx+0.9f, sHeight, wz+0.2f);
        glVertex3f(wx+0.9f, sHeight+0.4f, wz+0.2f); glVertex3f(wx+0.1f, sHeight+0.4f, wz+0.2f);
        glEnd();
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
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
        case VOXEL_SHAPE_VERTICAL_SPRITE: h = 1.0f; isSprite = true; break;
        case VOXEL_SHAPE_DECORATION: h = 0.3f; break;
        case VOXEL_SHAPE_DOOR: h = 0.05f; break;
        case VOXEL_SHAPE_STAIRS: h = 0.2f; break;
        case VOXEL_SHAPE_WALL: h = 0.0f; break; // Handled by VoxelMesh_BuildWalls
        case VOXEL_SHAPE_WALL_TOP: h = 0.0f; break;
        // Furniture
        case VOXEL_SHAPE_FURNITURE_TABLE:
        case VOXEL_SHAPE_FURNITURE_CHAIR:
        case VOXEL_SHAPE_FURNITURE_SHELF:
        case VOXEL_SHAPE_FURNITURE_TV:
        case VOXEL_SHAPE_FURNITURE_BED:
            VoxelMesh_DrawFurniture(mapX, mapY, shape, u0, v0, u1, v1);
            return;
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

    bool textureOnFront = false;
    
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
