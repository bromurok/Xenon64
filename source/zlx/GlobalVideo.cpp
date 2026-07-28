#include "GlobalVideo.h"
namespace ZLX {
    CxeGraphicsContext* g_pVideoDevice = nullptr;
    void InitialiseVideo() {
        // Reutiliza el singleton real de CGraphicsContext (Rice) en vez de
        // crear un segundo device D3D9 independiente, que competia con el
        // primero por el mismo hardware y rompia el ring buffer del GPU.
        g_pVideoDevice = (CxeGraphicsContext*)CGraphicsContext::g_pGraphicsContext;
    }
    void* loadPNGFromMemory(const unsigned char* data) {
        // TODO: usar D3DXCreateTextureFromFileInMemory real aqui
        return nullptr;
    }
}
