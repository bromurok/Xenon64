#pragma once
#include "../../xenon/d3d9_backend.h"
namespace ZLX {
    extern CxeGraphicsContext* g_pVideoDevice;
    void InitialiseVideo();
    void* loadPNGFromMemory(const unsigned char* data);
}
