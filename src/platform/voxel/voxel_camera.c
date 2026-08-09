#include "voxel_camera.h"

#ifdef PLATFORM_SDL2
#ifdef NATIVE_LINUX

#include <math.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include "voxel_world.h"

void VoxelCamera_Init(VoxelCamera *cam)
{
    cam->x = 0.0f;
    cam->y = 8.0f;
    cam->z = 0.0f;
    cam->targetX = 0.0f;
    cam->targetZ = 0.0f;
    cam->pitch = 40.0f;  // 35-50 degrees
    cam->yaw = 0.0f;
    cam->distance = 9.0f;
    cam->fov = 35.0f; // Tighter framing
}

void VoxelCamera_Update(VoxelCamera *cam, float playerWorldX, float playerWorldZ)
{
    cam->targetX += (playerWorldX - cam->targetX) * 0.15f;
    cam->targetZ += (playerWorldZ - cam->targetZ) * 0.15f;
    
    // Adapt distance to map size
    int mapW = 0, mapH = 0;
    VoxelWorld_GetMapDimensions(&mapW, &mapH);
    float baseDist = 8.0f;
    float mapScale = (float)(mapW > mapH ? mapW : mapH) * 0.2f;
    if (mapScale > 5.0f) mapScale = 5.0f;
    cam->distance = baseDist + mapScale;

    float pitchRad = cam->pitch * (3.14159265f / 180.0f);
    float yawRad   = cam->yaw   * (3.14159265f / 180.0f);

    // Camera sits to the SOUTH of the player (+Z) and above, looking NORTH (-Z).
    // With yaw=0:
    //   sinf(0)=0, cosf(0)=1
    //   cam.x = targetX + 0 = targetX  (no east/west offset)
    //   cam.z = targetZ + distance      (south of player)
    // The camera looks FROM (targetZ+dist) TOWARD (targetZ), i.e., it looks north.
    // Pressing UP = moving NORTH = decreasing Z = moving AWAY from camera = toward top of screen. ✓
    cam->x = cam->targetX + sinf(yawRad) * cam->distance;
    cam->y = tanf(pitchRad) * cam->distance;
    cam->z = cam->targetZ + cosf(yawRad) * cam->distance;
}

void VoxelCamera_ApplyProjection(const VoxelCamera *cam, int viewportW, int viewportH)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = (float)viewportW / (float)viewportH;
    gluPerspective(cam->fov, aspect, 0.1f, 200.0f);
}

void VoxelCamera_ApplyView(const VoxelCamera *cam)
{
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(cam->x, cam->y, cam->z,
              cam->targetX, 0.0f, cam->targetZ,
              0.0f, 1.0f, 0.0f);
}

#endif // NATIVE_LINUX
#endif // PLATFORM_SDL2
