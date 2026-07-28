#pragma once
#include <d3d9.h>

struct TVertex;
extern struct IDirect3DDevice9 * g_pd3dDevice;
extern struct IDirect3DVertexDeclaration9 * g_pVertexDecl;
extern struct IDirect3DVertexShader9 * g_pVS;
extern struct IDirect3DPixelShader9 * g_pPS_FB;

namespace ZLX {
namespace Draw {

    inline void Setup2DState() {
        if (!g_pd3dDevice) return;
        g_pd3dDevice->SetVertexDeclaration(g_pVertexDecl);
        g_pd3dDevice->SetVertexShader(g_pVS);
        g_pd3dDevice->SetPixelShader(g_pPS_FB);
        BOOL bFalse[4] = {0,0,0,0};
        g_pd3dDevice->SetPixelShaderConstantB(0, bFalse, 1);
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        g_pd3dDevice->SetTexture(0, NULL);
    }

    inline void SetOrthoMatrix2D() {
        if (!g_pd3dDevice) return;
        float W = 640.0f;
        float H = 480.0f;
        float ortho[16] = {
            2.0f/W,  0,       0,  -1,
            0,      -2.0f/H,  0,   1,
            0,       0,       1,   0,
            0,       0,       0,   1
        };
        g_pd3dDevice->SetVertexShaderConstantF(0, ortho, 4);
    }

    inline void DrawTexturedRect(float x, float y, float w, float h, void* tex) {
        if (!g_pd3dDevice || !tex) return;
        Setup2DState();
        SetOrthoMatrix2D();
        BOOL bTrue[4] = {1,0,0,0};
        g_pd3dDevice->SetPixelShaderConstantB(0, bTrue, 1);
        g_pd3dDevice->SetTexture(0, (IDirect3DBaseTexture9*)tex);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

        struct LocalVertex {
            float x, y, z, w;
            float u0, v0;
            float u1, v1;
            unsigned long color;
        };
        LocalVertex verts[4] = {
            { x,     y,     0.0f, 1.0f, 0,0, 0,0, 0xFFFFFFFF },
            { x + w, y,     0.0f, 1.0f, 1,0, 0,0, 0xFFFFFFFF },
            { x,     y + h, 0.0f, 1.0f, 0,1, 0,0, 0xFFFFFFFF },
            { x + w, y + h, 0.0f, 1.0f, 1,1, 0,0, 0xFFFFFFFF },
        };
        g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(LocalVertex));
        g_pd3dDevice->SetTexture(0, NULL);
    }

    inline void DrawColoredRect(float x, float y, float w, float h, unsigned int color) {
        if (!g_pd3dDevice) return;
        Setup2DState();
        SetOrthoMatrix2D();

        struct LocalVertex {
            float x, y, z, w;
            float u0, v0;
            float u1, v1;
            unsigned long color;
        };
        LocalVertex verts[4] = {
            { x,     y,     0.0f, 1.0f, 0,0, 0,0, color },
            { x + w, y,     0.0f, 1.0f, 0,0, 0,0, color },
            { x,     y + h, 0.0f, 1.0f, 0,0, 0,0, color },
            { x + w, y + h, 0.0f, 1.0f, 0,0, 0,0, color },
        };
        g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(LocalVertex));
    }
}
}
