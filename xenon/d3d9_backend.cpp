// Reemplazo de xenos_backend.cpp para XDK / Xbox 360 (.xex)
//
// TABLA DE EQUIVALENCIAS (de la llamada Xe_ original a la de D3D9 del XDK):
//
//   Xe_SetCullMode(xe, m)              -> g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, m)
//   Xe_SetScissor(xe, r)               -> g_pd3dDevice->SetScissorRect(&rect)
//   Xe_SetBlendControl(xe, s, d)       -> SetRenderState(D3DRS_SRCBLEND,...)/(D3DRS_DESTBLEND,...)
//   Xe_SetZWrite(xe, b)                -> SetRenderState(D3DRS_ZWRITEENABLE, b)
//   Xe_SetZEnable(xe, b)               -> SetRenderState(D3DRS_ZENABLE, b)
//   Xe_SetZFunc(xe, f)                 -> SetRenderState(D3DRS_ZFUNC, f)
//   Xe_SetRenderTarget(xe, surf)       -> SetRenderTarget(0, surf)
//   Xe_SetFillMode(xe, m)              -> SetRenderState(D3DRS_FILLMODE, m)
//   Xe_SetAlphaTestEnable(xe, b)       -> SetRenderState(D3DRS_ALPHATESTENABLE, b)
//   Xe_SetAlphaFunc(xe, f)             -> SetRenderState(D3DRS_ALPHAFUNC, f)
//   Xe_SetAlphaRef(xe, v)              -> SetRenderState(D3DRS_ALPHAREF, v)
//   Xe_SetClearColor(xe, c)            -> (se pasa directo a Clear)
//   Xe_Clear(xe, flags, color, depth, stencil) -> g_pd3dDevice->Clear(...)
//   Xe_SetTexture(xe, stage, tex)      -> SetTexture(stage, tex)
//   Xe_CreateTexture(w,h,levels,fmt)   -> CreateTexture(w,h,levels,0,fmt,0,&tex)
//   Xe_DestroyTexture(tex)             -> tex->Release()
//   Xe_Surface_LockRect / Unlock       -> tex->LockRect(0,&lr,NULL,0) / UnlockRect(0)
//   Xe_SetShader(xe, type, shader, n)  -> SetVertexShader()/SetPixelShader()
//   Xe_InstantiateShader / LoadShaderFromMemory -> D3DXCompileShader o .xpu precompilado
//   Xe_SetPixelShaderConstantF/B       -> SetPixelShaderConstantF/B
//   Xe_SetVertexShaderConstantF        -> SetVertexShaderConstantF
//   Xe_SetStreamSource(xe, i, vb, off, stride) -> SetStreamSource(i, vb, off, stride)
//   Xe_VB_Lock / Unlock                -> vb->Lock(offset,size,&ptr,flags) / Unlock()
//   Xe_CreateVertexBuffer(size)        -> CreateVertexBuffer(size,0,0,D3DPOOL_DEFAULT,&vb,NULL)
//   Xe_DrawPrimitive(xe,type,start,n)  -> DrawPrimitive(type, start, n)
//   Xe_Execute / Xe_Sync / Xe_InvalidateState -> no aplica igual en D3D9 normal;
//         D3D9 en Xbox 360 ejecuta el comando al llamar cada método, no hace
//         falta un "Execute" de cola manual como en el acceso directo a Xenos.
//         Xe_Sync se sustituye por g_pd3dDevice->Present(...) al final de frame
//         (o BlockUntilIdle si hace falta esperar GPU antes de leer memoria).
//   Xe_GetFramebufferSurface / SetFrameBufferSurface -> GetRenderTarget(0,&surf) /
//         SetRenderTarget(0, surf)
//   Xe_ResolveInto                     -> Resolve() (D3D9 Xbox 360 tiene Resolve
//         nativo de EDRAM a memoria de textura, es un concepto que SÍ existe
//         igual en el XDK, revisar D3DRESOLVE_* flags)
//   Xemit_* (generación de microcódigo de shaders)  -> se reemplaza por
//         D3DXCompileShader (HLSL a shader binario) en tiempo de build o runtime;
//         los combiners generados dinámicamente en GenerateShader() pasan de
//         emitir microcódigo Xenos a mano a compilar un HLSL generado como string.
//
// Todo lo de este archivo vive DETRÁS de las mismas clases que ya usa el
// resto de Rice_GX_Xenos (CxeRender, CxeTexture, CxeColorCombiner, etc.),
// así que Render.cpp / TextureManager.cpp / CombinerTable.cpp no cambian.

#include "stdafx.h"
#include "d3d9_backend.h"
#include <d3dx9.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../source/Rice_GX_Xenos/dbgswitch.h"

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif

#define MAKE_COLOR3(r,g,b) (0xff000000 | ((b)<<16) | ((g)<<8) | (r))
#define MAKE_COLOR4(r,g,b,a) ((a)<<24 | ((b)<<16) | ((g)<<8) | (r))
#define MAKE_COLOR1F(c) ((u8)(255.0f*((c)>1.0f?1.0f:(c))))
#define MAKE_COLOR4F(r,g,b,a) (MAKE_COLOR1F(a)<<24 | (MAKE_COLOR1F(b)<<16) | (MAKE_COLOR1F(g)<<8) | MAKE_COLOR1F(r))

#define MAX_VERTEX_COUNT (16384*3)

#define NEAR_PLANE (-10.0f)
#define FAR_PLANE  (10.0f)

// Formato de vertice: igual estructura que el original (XenosVBFFormat),
// ahora expresado como D3DVERTEXELEMENT9 para el XDK.
typedef struct
{
    float x,y,z,w;
    float u0,v0;
    float u1,v1;
    unsigned long color;
} TVertex;

static const D3DVERTEXELEMENT9 VertexDecl[] =
{
    { 0, 0,  D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
    { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
    { 0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },
    { 0, 32, D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
    D3DDECL_END()
};

// ---- Globales equivalentes a los del original ----
IDirect3DDevice9 * g_pd3dDevice = NULL;
IDirect3DVertexDeclaration9 * g_pVertexDecl = NULL;
IDirect3DVertexShader9 * g_pVS = NULL;
IDirect3DPixelShader9 * g_pPS_FB = NULL;
static IDirect3DVertexBuffer9 * g_pVertexBuffer = NULL;

static int prevVertexCount = 0;
static TVertex * firstVertex = NULL;
static TVertex * currentVertex = NULL;

static int xe_cull;
static bool xe_zcompare;
static bool xe_zenable;
static bool xe_zwrite;
static bool xe_alphatest;

static float xe_origx = 0, xe_origy = 0, xe_scalex = 1, xe_scaley = 1;

static int vertexCount()
{
    return (int)(currentVertex - firstVertex);
}

// Equivalente a resetLockVB() del original
static void resetLockVB()
{
    g_pd3dDevice->SetStreamSource(0, NULL, 0, 0);
    void * pData = NULL;
    g_pVertexBuffer->Lock(0, MAX_VERTEX_COUNT * sizeof(TVertex), &pData, 0);
    firstVertex = currentVertex = (TVertex*)pData;
    prevVertexCount = 0;
    g_pd3dDevice->BeginScene();
}

// Equivalente a nextVertex()
static void nextVertex()
{
    ++currentVertex;
    if (vertexCount() >= MAX_VERTEX_COUNT)
    {
        printf("[d3d9_backend] demasiados vertices!\n");
        exit(1);
    }
}

// Equivalente a drawVB() -> Xe_DrawPrimitive
// En el original xenos, el VB queda locked todo el frame y el GPU lee directo.
// En D3D9 Xbox 360 la memoria es unificada, asi que funciona igual.
static void drawVB()
{
    if (vertexCount() > prevVertexCount)
    {
        int numTris = (vertexCount() - prevVertexCount) / 3;
        static int totalDraws = 0;
        totalDraws++;

        if (totalDraws <= 5 || (totalDraws % 100 == 0))
        {
            char dbg[256];
            sprintf(dbg, "[d3d9] drawVB #%d: start=%d count=%d tris=%d totalVtx=%d",
                totalDraws, prevVertexCount, vertexCount() - prevVertexCount, numTris, vertexCount());
            DLOG(dbg);
        }

        g_pd3dDevice->SetStreamSource(0, g_pVertexBuffer, 0, sizeof(TVertex));
        g_pd3dDevice->DrawPrimitive(D3DPT_TRIANGLELIST, prevVertexCount, numTris);
        prevVertexCount = vertexCount();
    }
}

// =====================================================================
// A PARTIR DE AQUÍ: portar función por función desde xenos_backend.cpp
// original usando la tabla de arriba. Cada método de CxeGraphicsContext,
// CxeRender, CxeBlender, CxeTexture y CxeColorCombiner que estaba en el
// original debe reimplementarse aquí contra g_pd3dDevice. Dejo los stubs
// de los métodos que Render.cpp / TextureManager.cpp llaman directamente,
// para que el link no falle mientras se van completando uno a uno.
// =====================================================================

// ---------------- CxeGraphicsContext ----------------

CxeGraphicsContext::CxeGraphicsContext() {}
CxeGraphicsContext::~CxeGraphicsContext() {}

bool CxeGraphicsContext::Initialize(HWND hWnd, HWND hWndStatus, uint32 dwWidth, uint32 dwHeight, BOOL bWindowed)
{
    char dbgbuf[256];
    sprintf(dbgbuf, "[d3d9_backend] Initialize(%ux%u)\n", (unsigned int)dwWidth, (unsigned int)dwHeight);
    OutputDebugStringA(dbgbuf);

    // Set display dimensions BEFORE creating the device, so that glViewportWrapper
    // produces a valid ortho matrix (avoids 2.0/0 = INF → NaN → black screen).
    // Must set both Window/FullScreen variants AND the active uDisplay variants,
    // because glViewportWrapper uses uDisplayWidth/uDisplayHeight.
    windowSetting.uWindowDisplayWidth  = dwWidth;
    windowSetting.uWindowDisplayHeight = dwHeight;
    windowSetting.uFullScreenDisplayWidth  = dwWidth;
    windowSetting.uFullScreenDisplayHeight = dwHeight;
    windowSetting.uDisplayWidth  = dwWidth;
    windowSetting.uDisplayHeight = dwHeight;

    IDirect3D9 * pd3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pd3d)
    {
        OutputDebugStringA("[d3d9_backend] Direct3DCreate9 fallo\n");
        return false;
    }

    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.BackBufferWidth        = dwWidth;
    d3dpp.BackBufferHeight       = dwHeight;
    d3dpp.BackBufferFormat       = D3DFMT_A8R8G8B8;
    d3dpp.BackBufferCount        = 1;
    d3dpp.MultiSampleType        = D3DMULTISAMPLE_NONE;
    d3dpp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D24S8;
    d3dpp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    d3dpp.PresentationInterval   = D3DPRESENT_INTERVAL_ONE;

    HRESULT hr = pd3d->CreateDevice(
        0,
        D3DDEVTYPE_HAL,
        NULL,
        D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &d3dpp,
        &g_pd3dDevice);

    pd3d->Release();

    if (FAILED(hr) || !g_pd3dDevice)
    {
        sprintf(dbgbuf, "[d3d9_backend] CreateDevice fallo (hr=0x%08X)\n", (unsigned int)hr);
        OutputDebugStringA(dbgbuf);
        return false;
    }

    OutputDebugStringA("[d3d9_backend] CreateDevice OK\n");

    // Explicit D3D9 viewport — without this, draws may produce nothing.
    D3DVIEWPORT9 vp;
    ZeroMemory(&vp, sizeof(vp));
    vp.X      = 0;
    vp.Y      = 0;
    vp.Width  = dwWidth;
    vp.Height = dwHeight;
    vp.MinZ   = 0.0f;
    vp.MaxZ   = 1.0f;
    g_pd3dDevice->SetViewport(&vp);

    // Default render state
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);

    g_pd3dDevice->CreateVertexBuffer(
        MAX_VERTEX_COUNT * sizeof(TVertex),
        D3DUSAGE_WRITEONLY,
        0,
        D3DPOOL_DEFAULT,
        &g_pVertexBuffer,
        NULL);

    g_pd3dDevice->CreateVertexDeclaration(VertexDecl, &g_pVertexDecl);
    g_pd3dDevice->SetVertexDeclaration(g_pVertexDecl);

    {
        auto LoadShaderBytecode = [](const char* path, void** outBuf, long* outSize) -> bool
        {
            FILE* f = fopen(path, "rb");
            if (!f)
            {
                char dbg[256]; sprintf(dbg, "[d3d9_backend] no se pudo abrir %s\n", path);
                OutputDebugStringA(dbg);
                return false;
            }
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            void* buf = malloc(size);
            size_t read = fread(buf, 1, size, f);
            fclose(f);
            if ((long)read != size)
            {
                char dbg[256]; sprintf(dbg, "[d3d9_backend] lectura incompleta de %s (%zu/%ld)\n", path, read, size);
                OutputDebugStringA(dbg);
                free(buf);
                return false;
            }
            *outBuf = buf;
            *outSize = size;
            return true;
        };

        {
            const char* vsHLSL =
                "struct _IN { float4 pos:POSITION; float2 uv0:TEXCOORD0; float2 uv1:TEXCOORD1; float4 col:COLOR; };\n"
                "struct _OUT { float4 pos:POSITION; float2 uv0:TEXCOORD0; float2 uv1:TEXCOORD1; float4 col:COLOR; };\n"
                "float4x4 ortho:register(c0);\n"
                "_OUT main(_IN In){\n"
                "  _OUT Out;\n"
                "  Out.pos=mul(In.pos,ortho);\n"
                "  Out.col=In.col;\n"
                "  Out.uv0=In.uv0;\n"
                "  Out.uv1=In.uv1;\n"
                "  return Out;\n"
                "}\n";
            LPD3DXBUFFER pVSCode = NULL, pVSErr = NULL;
            HRESULT hrVS = D3DXCompileShader(vsHLSL, (UINT)strlen(vsHLSL),
                NULL, NULL, "main", "vs_2_0", 0, &pVSCode, &pVSErr, NULL);
            if (FAILED(hrVS))
            {
                sprintf(dbgbuf, "[d3d9] VS compile FALLO (hr=0x%08X): %s\n",
                    (unsigned int)hrVS, pVSErr ? (char*)pVSErr->GetBufferPointer() : "no error");
                OutputDebugStringA(dbgbuf);
                DLOG(dbgbuf);
                if (pVSErr) pVSErr->Release();
            }
            else
            {
                HRESULT hr2 = g_pd3dDevice->CreateVertexShader((DWORD*)pVSCode->GetBufferPointer(), &g_pVS);
                sprintf(dbgbuf, "[d3d9] VS runtime compile OK, CreateVertexShader hr=0x%08X vs=%p",
                    (unsigned int)hr2, (void*)g_pVS);
                OutputDebugStringA(dbgbuf);
                DLOG(dbgbuf);
                pVSCode->Release();
            }
        }

        {
            const char* psHLSL =
                "sampler tex0:register(s0);\n"
                "bool use_tex:register(b0);\n"
                "struct _IN{float2 uv0:TEXCOORD0;float2 uv1:TEXCOORD1;float4 col:COLOR;};\n"
                "float4 main(_IN data):COLOR{\n"
                "  if(use_tex){return tex2D(tex0,data.uv0);}\n"
                "  else{return data.col;}\n"
                "}\n";
            LPD3DXBUFFER pPSCode = NULL, pPSErr = NULL;
            HRESULT hrPS = D3DXCompileShader(psHLSL, (UINT)strlen(psHLSL),
                NULL, NULL, "main", "ps_2_0", 0, &pPSCode, &pPSErr, NULL);
            if (FAILED(hrPS))
            {
                sprintf(dbgbuf, "[d3d9] PS compile FALLO (hr=0x%08X): %s\n",
                    (unsigned int)hrPS, pPSErr ? (char*)pPSErr->GetBufferPointer() : "no error");
                OutputDebugStringA(dbgbuf);
                DLOG(dbgbuf);
                if (pPSErr) pPSErr->Release();
            }
            else
            {
                HRESULT hr2 = g_pd3dDevice->CreatePixelShader((DWORD*)pPSCode->GetBufferPointer(), &g_pPS_FB);
                sprintf(dbgbuf, "[d3d9] PS runtime compile OK, CreatePixelShader hr=0x%08X ps=%p",
                    (unsigned int)hr2, (void*)g_pPS_FB);
                OutputDebugStringA(dbgbuf);
                DLOG(dbgbuf);
                pPSCode->Release();
            }
        }

        if (g_pVS)    g_pd3dDevice->SetVertexShader(g_pVS);
        if (g_pPS_FB) g_pd3dDevice->SetPixelShader(g_pPS_FB);

        sprintf(dbgbuf, "[d3d9] Initialize shaders: g_pVS=%p g_pPS_FB=%p", (void*)g_pVS, (void*)g_pPS_FB);
        OutputDebugStringA(dbgbuf);
        DLOG(dbgbuf);
    }

    resetLockVB();

    DLOG("[d3d9] Initialize complete");
    return true;
}

void CxeGraphicsContext::CleanUp()
{
    if (g_pVertexBuffer) { g_pVertexBuffer->Release(); g_pVertexBuffer = NULL; }
    if (g_pVertexDecl) { g_pVertexDecl->Release(); g_pVertexDecl = NULL; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
}

void CxeGraphicsContext::Clear(ClearFlag dwFlags, uint32 color, float depth)
{
    char dbg[256];
    sprintf(dbg, "[d3d9] Clear flags=0x%08X color=0x%08X depth=%.3f", (unsigned int)dwFlags, (unsigned int)color, depth);
    DLOG(dbg);

    DWORD flags = 0;
    if (dwFlags & CLEAR_COLOR_BUFFER) flags |= D3DCLEAR_TARGET;
    if (dwFlags & CLEAR_DEPTH_BUFFER) flags |= D3DCLEAR_ZBUFFER;
    g_pd3dDevice->Clear(0, NULL, flags, color, depth, 0);
}

void CxeGraphicsContext::UpdateFrame(bool swaponly)
{
    status.gFrameCount++;

    if (firstVertex)
    {
        int vtxCount = vertexCount();
        char dbg[256];
        sprintf(dbg, "[d3d9] UpdateFrame: vtxCount=%d prevVertexCount=%d g_pVS=%p g_pPS_FB=%p",
            vtxCount, prevVertexCount, (void*)g_pVS, (void*)g_pPS_FB);
        DLOG(dbg);

        g_pVertexBuffer->Unlock();
        firstVertex = NULL;
        currentVertex = NULL;

        HRESULT hrEnd = g_pd3dDevice->EndScene();
        HRESULT hrPres = g_pd3dDevice->Present(NULL, NULL, NULL, NULL);

        sprintf(dbg, "[d3d9] Present hr=0x%08X EndScene hr=0x%08X gFrameCount=%u",
            (unsigned int)hrPres, (unsigned int)hrEnd, status.gFrameCount);
        DLOG(dbg);

        // Xe_InvalidateState equivalent: reset GPU state so next frame starts clean
        if (g_pVS) g_pd3dDevice->SetVertexShader(g_pVS);
        else g_pd3dDevice->SetVertexShader(NULL);
        if (g_pPS_FB) g_pd3dDevice->SetPixelShader(g_pPS_FB);
        else g_pd3dDevice->SetPixelShader(NULL);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
        g_pd3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        g_pd3dDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

        if (g_curRomInfo.bForceScreenClear) needCleanScene = true;
        status.bScreenIsDrawn = false;
    }

    resetLockVB();
}

int CxeGraphicsContext::ToggleFullscreen() { return 0; }

bool CxeGraphicsContext::IsExtensionSupported(const char* pExtName) { return false; }
bool CxeGraphicsContext::IsWglExtensionSupported(const char* pExtName) { return false; }
void CxeGraphicsContext::InitDeviceParameters() {}
void CxeGraphicsContext::InitState(void) {}
void CxeGraphicsContext::InitOGLExtension(void) {}
bool CxeGraphicsContext::SetFullscreenMode() { return true; }
bool CxeGraphicsContext::SetWindowMode() { return true; }
bool CxeGraphicsContext::IsSupportAnisotropicFiltering() { return true; }
int  CxeGraphicsContext::getMaxAnisotropicFiltering() { return 16; }

// ---------------- CxeRender ----------------
// TODO: portar cada uno de estos usando la tabla de equivalencias.
// Dejo implementados los más directos (1 línea) como ejemplo de patrón;
// el resto son intencionalmente stubs para completar con el original al lado.

CxeRender::CxeRender()
{
    m_bSupportFogCoordExt = false;
    m_bSupportClampToEdge = true;

    m_maxTexUnits = 8;

    for( int i=0; i<8; i++ )
    {
        m_curBoundTex[i]=0;
        m_textureUnitMap[i] = -1;
        m_texUnitEnabled[i]=FALSE;
    }

    m_textureUnitMap[0] = 0;
    m_textureUnitMap[1] = 1;
}
CxeRender::~CxeRender() {}
bool CxeRender::InitDeviceObjects() { return true; }
bool CxeRender::ClearDeviceObjects() { return true; }

void CxeRender::Initialize(void)
{
    m_bPolyOffset = false;
    if (g_pVS) g_pd3dDevice->SetVertexShader(g_pVS);
    if (g_pPS_FB) g_pd3dDevice->SetPixelShader(g_pPS_FB);
    glViewportWrapper(0, windowSetting.statusBarHeightToUse, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight, true);
    RenderReset();
}

void CxeRender::SetCullMode(bool bCullFront, bool bCullBack)
{
    CRender::SetCullMode(bCullFront, bCullBack);
    DWORD mode = D3DCULL_NONE;
    if (bCullFront && !bCullBack) mode = D3DCULL_CCW;
    else if (bCullBack && !bCullFront) mode = D3DCULL_CW;
    xe_cull = mode; // mantener sincronizada la variable global que usan
                     // RenderTexRect/RenderFillRect/DrawSimple2DTexture, etc.
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, mode);
}

void CxeRender::ZBufferEnable(BOOL bZBuffer)
{
    gRSP.bZBufferEnabled = bZBuffer;
    if (g_curRomInfo.bForceDepthBuffer)
        bZBuffer = TRUE;
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, bZBuffer ? TRUE : FALSE);
}

void CxeRender::SetZCompare(BOOL bZCompare)
{
    if (g_curRomInfo.bForceDepthBuffer)
        bZCompare = TRUE;
    gRSP.bZBufferEnabled = bZCompare;
    g_pd3dDevice->SetRenderState(D3DRS_ZFUNC, bZCompare ? D3DCMP_LESSEQUAL : D3DCMP_ALWAYS);
}

void CxeRender::SetZUpdate(BOOL bZUpdate)
{
    if (g_curRomInfo.bForceDepthBuffer)
        bZUpdate = TRUE;
    g_pd3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, bZUpdate ? TRUE : FALSE);
}

void CxeRender::SetAlphaTestEnable(BOOL bAlphaTestEnable)
{
    g_pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, bAlphaTestEnable ? TRUE : FALSE);
    if (bAlphaTestEnable)
        g_pd3dDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
}

void CxeRender::SetAlphaRef(uint32 dwAlpha)
{
    g_pd3dDevice->SetRenderState(D3DRS_ALPHAREF, dwAlpha);
}
void CxeRender::ForceAlphaRef(uint32 dwAlpha) { SetAlphaRef(dwAlpha); }

void CxeRender::SetFillMode(FillMode mode)
{
    g_pd3dDevice->SetRenderState(D3DRS_FILLMODE,
        (mode == RICE_FILLMODE_WINFRAME) ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
}

// ---------------- Scissor ----------------
void CxeRender::UpdateScissor()
{
    UpdateScissorWithClipRatio();
}

void CxeRender::ApplyRDPScissor(bool force)
{
    if( !force && status.curScissor == RDP_SCISSOR )    return;

    RECT r;
    r.left   = (LONG) max(xe_origx + (gRDP.scissor.left*windowSetting.fMultX) * xe_scalex, 0);
    r.top    = (LONG) max(xe_origy + (gRDP.scissor.top*windowSetting.fMultY) * xe_scaley, 0);
    r.right  = (LONG) max(xe_origx + min((gRDP.scissor.right*windowSetting.fMultX) * xe_scalex,windowSetting.uDisplayWidth), 0);
    r.bottom = (LONG) max(xe_origy + min((gRDP.scissor.bottom*windowSetting.fMultY) * xe_scaley,windowSetting.uDisplayHeight), 0);
    g_pd3dDevice->SetScissorRect(&r);

    status.curScissor = RDP_SCISSOR;
}

void CxeRender::ApplyScissorWithClipRatio(bool force)
{
    if( !force && status.curScissor == RSP_SCISSOR )    return;

    RECT r;
    r.left   = (LONG) max(xe_origx + windowSetting.clipping.left * xe_scalex, 0);
    r.top    = (LONG) max(xe_origy + ((gRSP.real_clip_scissor_top)*windowSetting.fMultY) * xe_scaley, 0);
    r.right  = (LONG) max(xe_origx + min((windowSetting.clipping.left+windowSetting.clipping.width-1) * xe_scalex,windowSetting.uDisplayWidth), 0);
    r.bottom = (LONG) max(xe_origy + min(((gRSP.real_clip_scissor_top*windowSetting.fMultY) + windowSetting.clipping.height-1) * xe_scaley,windowSetting.uDisplayHeight), 0);
    g_pd3dDevice->SetScissorRect(&r);

    status.curScissor = RSP_SCISSOR;
}

// ---------------- Fog ----------------
void CxeRender::SetFogMinMax(float fMin, float fMax)
{
}

void CxeRender::TurnFogOnOff(bool flag)
{
}

void CxeRender::SetFogEnable(bool bEnable)
{
    gRSP.bFogEnabled = bEnable;
}

void CxeRender::SetFogColor(uint32 r, uint32 g, uint32 b, uint32 a)
{
    gRDP.fogColor = COLOR_RGBA(r, g, b, a);
    gRDP.fvFogColor[0] = r/255.0f;
    gRDP.fvFogColor[1] = g/255.0f;
    gRDP.fvFogColor[2] = b/255.0f;
    gRDP.fvFogColor[3] = a/255.0f;
}
// ---------------- Otros ----------------
void CxeRender::EndRendering(void)
{
    if( CRender::gRenderReferenceCount > 0 )
        CRender::gRenderReferenceCount--;
    }

// ---------------- Clear ----------------
void CxeRender::ClearBuffer(bool cbuffer, bool zbuffer)
{
    static int clearCount = 0;
    clearCount++;

    float depth = ((gRDP.originalFillColor & 0xFFFF) >> 2) / (float)0x3FFF;

    DWORD flags = 0;
    if (cbuffer) flags |= D3DCLEAR_TARGET;
    if (zbuffer) flags |= D3DCLEAR_ZBUFFER;

    if (clearCount <= 10 || (clearCount % 500 == 0))
    {
        char dbg[256];
        sprintf(dbg, "[d3d9] ClearBuffer #%d flags=0x%08X cbuf=%d zbuf=%d depth=%.4f",
            clearCount, (unsigned int)flags, cbuffer, zbuffer, depth);
        DLOG(dbg);
    }

    g_pd3dDevice->Clear(0, NULL, flags, D3DCOLOR_XRGB(0,0,0), depth, 0);
}

void CxeRender::ClearZBuffer(float depth)
{
    static int clearZCount = 0;
    clearZCount++;
    if (clearZCount <= 10 || (clearZCount % 500 == 0))
    {
        char dbg[256];
        sprintf(dbg, "[d3d9] ClearZBuffer #%d depth=%.4f", clearZCount, depth);
        DLOG(dbg);
    }
    g_pd3dDevice->Clear(0, NULL, D3DCLEAR_ZBUFFER, 0, depth, 0);
}

void CxeRender::ApplyZBias(int bias)
{
    m_bPolyOffset = (bias > 0);
}

void CxeRender::SetZBias(int bias)
{
    m_dwZBias = bias;
    ApplyZBias(bias);
}

// ---------------- Texturas ----------------
bool CxeRender::SetCurrentTexture(int tile, CTexture *handler, uint32 dwTileWidth, uint32 dwTileHeight, TxtrCacheEntry *pTextureEntry)
{
    RenderTexture &texture = g_textures[tile];
    texture.pTextureEntry = pTextureEntry;

    if (handler != NULL && texture.m_lpsTexturePtr != handler->GetTexture())
    {
        texture.m_pCTexture = handler;
        texture.m_pCxeTexture = (CxeTexture*)handler;
        texture.m_lpsTexturePtr = handler->GetTexture();

        texture.m_dwTileWidth = dwTileWidth;
        texture.m_dwTileHeight = dwTileHeight;

        if (handler->m_bIsEnhancedTexture)
        {
            texture.m_fTexWidth = (float)pTextureEntry->pTexture->m_dwCreatedTextureWidth;
            texture.m_fTexHeight = (float)pTextureEntry->pTexture->m_dwCreatedTextureHeight;
        }
        else
        {
            texture.m_fTexWidth = (float)handler->m_dwCreatedTextureWidth;
            texture.m_fTexHeight = (float)handler->m_dwCreatedTextureHeight;
        }

        static int texSetCount = 0;
        texSetCount++;
        if (texSetCount <= 10 || (texSetCount % 500 == 0))
        {
            CxeTexture* xtex = (CxeTexture*)handler;
            char dbg[256]; sprintf(dbg, "[d3d9] SetTexture #%d tile=%d handler=%p d3dtex=%p %ux%u",
                texSetCount, tile, (void*)handler, xtex ? (void*)xtex->tex : NULL,
                (unsigned int)dwTileWidth, (unsigned int)dwTileHeight);
            DLOG(dbg);
        }
    }

    return true;
}

bool CxeRender::SetCurrentTexture(int tile, TxtrCacheEntry *pEntry)
{
    if (pEntry != NULL && pEntry->pTexture != NULL)
    {
        SetCurrentTexture(tile, pEntry->pTexture, pEntry->ti.WidthToCreate, pEntry->ti.HeightToCreate, pEntry);
        return true;
    }
    else
    {
        SetCurrentTexture(tile, NULL, 64, 64, NULL);
        return false;
    }
}

void CxeRender::SetTexWrapS(int unitno, u32 flag)
{
    assert(false);
}

void CxeRender::SetTexWrapT(int unitno, u32 flag)
{
    assert(false);
}

void CxeRender::SetTextureUFlag(TextureUVFlag dwFlag, uint32 dwTile)
{
    TileUFlags[dwTile] = dwFlag;
    int tex;
    if (dwTile == gRSP.curTile)
        tex = 0;
    else if (dwTile == ((gRSP.curTile + 1) & 7))
        tex = 1;
    else if (dwTile == ((gRSP.curTile + 2) & 7))
        tex = 2;
    else if (dwTile == ((gRSP.curTile + 3) & 7))
        tex = 3;
    else
    {
        TRACE2("Incorrect tile number for D3D9 SetTextureUFlag: cur=%d, tile=%d", gRSP.curTile, dwTile);
        return;
    }

    DWORD addr = D3DTADDRESS_WRAP;
    if (dwFlag == TEXTURE_UV_FLAG_MIRROR) addr = D3DTADDRESS_MIRROR;
    else if (dwFlag == TEXTURE_UV_FLAG_CLAMP) addr = D3DTADDRESS_CLAMP;

    for (int textureNo = 0; textureNo < 8; textureNo++)
    {
        if (m_textureUnitMap[textureNo] == tex)
        {
            CxeTexture* pTexture = g_textures[(gRSP.curTile + tex) & 7].m_pCxeTexture;
            if (pTexture)
            {
                EnableTexUnit(textureNo, TRUE);

                if (pTexture->tex)
                {
                    g_pd3dDevice->SetSamplerState(textureNo, D3DSAMP_ADDRESSU, addr);
                }

                BindTexture(pTexture, textureNo);
            }
        }
    }
}

void CxeRender::SetTextureVFlag(TextureUVFlag dwFlag, uint32 dwTile)
{
    TileVFlags[dwTile] = dwFlag;
    int tex;
    if (dwTile == gRSP.curTile)
        tex = 0;
    else if (dwTile == ((gRSP.curTile + 1) & 7))
        tex = 1;
    else if (dwTile == ((gRSP.curTile + 2) & 7))
        tex = 2;
    else if (dwTile == ((gRSP.curTile + 3) & 7))
        tex = 3;
    else
    {
        TRACE2("Incorrect tile number for D3D9 SetTextureVFlag: cur=%d, tile=%d", gRSP.curTile, dwTile);
        return;
    }

    DWORD addr = D3DTADDRESS_WRAP;
    if (dwFlag == TEXTURE_UV_FLAG_MIRROR) addr = D3DTADDRESS_MIRROR;
    else if (dwFlag == TEXTURE_UV_FLAG_CLAMP) addr = D3DTADDRESS_CLAMP;

    for (int textureNo = 0; textureNo < 8; textureNo++)
    {
        if (m_textureUnitMap[textureNo] == tex)
        {
            CxeTexture* pTexture = g_textures[(gRSP.curTile + tex) & 7].m_pCxeTexture;
            if (pTexture)
            {
                EnableTexUnit(textureNo, TRUE);

                if (pTexture->tex)
                {
                    g_pd3dDevice->SetSamplerState(textureNo, D3DSAMP_ADDRESSV, addr);
                }

                BindTexture(pTexture, textureNo);
            }
        }
    }
}

void CxeRender::SetTextureToTextureUnitMap(int tex, int unit)
{
    if (unit < 8 && (tex >= -1 || tex <= 1))
        m_textureUnitMap[unit] = tex;
}

void CxeRender::BindTexture(CxeTexture *texture, int unitno)
{
    if (unitno < m_maxTexUnits)
    {
        if (texture && texture->tex)
        {
            DWORD filter = (m_dwMinFilter == FILTER_LINEAR || m_dwMagFilter == FILTER_LINEAR)
                           ? D3DTEXF_LINEAR : D3DTEXF_POINT;
            g_pd3dDevice->SetSamplerState(unitno, D3DSAMP_MINFILTER, filter);
            g_pd3dDevice->SetSamplerState(unitno, D3DSAMP_MAGFILTER, filter);
            g_pd3dDevice->SetTexture(unitno, texture->tex);
            static int bindCount = 0;
            bindCount++;
            {
                char dbg[256]; sprintf(dbg, "[d3d9] BindTexture #%d unit=%d tex=%p (%ux%u)",
                    bindCount, unitno, (void*)texture->tex,
                    (unsigned int)texture->m_dwCreatedTextureWidth,
                    (unsigned int)texture->m_dwCreatedTextureHeight);
                DLOG(dbg);
            }
        }
        else
        {
            static int bindNullCount = 0;
            bindNullCount++;
            {
                char dbg[256]; sprintf(dbg, "[d3d9] BindTexture NULL #%d unit=%d texPtr=%p",
                    bindNullCount, unitno, (void*)(texture ? texture->tex : NULL));
                DLOG(dbg);
            }
            g_pd3dDevice->SetTexture(unitno, NULL);
        }
        m_curBoundTex[unitno] = texture;
    }
}

void CxeRender::EnableTexUnit(int unitno, BOOL flag)
{
    if (m_texUnitEnabled[unitno] != flag)
    {
        m_texUnitEnabled[unitno] = flag;
        BindTexture(m_curBoundTex[unitno], unitno);
    }
}

void CxeRender::ApplyTextureFilter()
{
    for (int i = 0; i < m_maxTexUnits; i++)
    {
        if (m_texUnitEnabled[i] && m_curBoundTex[i] && m_curBoundTex[i]->tex)
        {
            DWORD filter = (m_dwMinFilter == FILTER_LINEAR || m_dwMagFilter == FILTER_LINEAR)
                           ? D3DTEXF_LINEAR : D3DTEXF_POINT;
            g_pd3dDevice->SetSamplerState(i, D3DSAMP_MINFILTER, filter);
            g_pd3dDevice->SetSamplerState(i, D3DSAMP_MAGFILTER, filter);
        }
    }
}

void CxeRender::SetShadeMode(RenderShadeMode mode)
{
    // El original tampoco hace nada aca (Xenos maneja shading via el pixel shader del combiner)
}

void CxeRender::SetViewportRender()
{
    glViewportWrapper(windowSetting.vpLeftW,
        windowSetting.uDisplayHeight - windowSetting.vpTopW - windowSetting.vpHeightW + windowSetting.statusBarHeightToUse,
        windowSetting.vpWidthW, windowSetting.vpHeightW, true);
}

void CxeRender::RenderReset()
{
    CRender::RenderReset();

    g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

    if (g_pVS) g_pd3dDevice->SetVertexShader(g_pVS);
    m_pColorCombiner->DisableCombiner();
}

COLOR CxeRender::PostProcessDiffuseColor(COLOR curDiffuseColor)
{
    uint32 color = curDiffuseColor;
    uint32 colorflag = m_pColorCombiner->m_pDecodedMux->m_dwShadeColorChannelFlag;
    uint32 alphaflag = m_pColorCombiner->m_pDecodedMux->m_dwShadeAlphaChannelFlag;
    if (colorflag + alphaflag != MUX_0)
    {
        if ((colorflag & 0xFFFFFF00) == 0 && (alphaflag & 0xFFFFFF00) == 0)
        {
            color = (m_pColorCombiner->GetConstFactor(colorflag, alphaflag, curDiffuseColor));
        }
        else
            color = (CalculateConstFactor(colorflag, alphaflag, curDiffuseColor));
    }
    return color;
}

COLOR CxeRender::PostProcessSpecularColor()
{
    return 0;
}

void CxeRender::glViewportWrapper(int x, int y, int width, int height, bool ortho)
{
    static int mx=0,my=0;
    static int m_width=0, m_height=0;
    static bool mflag=true;
    static bool mbias=false;
    if (x!=mx || y!=my || width!=m_width || height!=m_height || mflag!=ortho || mbias!=m_bPolyOffset)
    {
        mx=x; my=y; m_width=width; m_height=height; mflag=ortho; mbias=m_bPolyOffset;

        // Update D3D9 viewport so the rasterizer doesn't clip to a zero-size rect.
        if (width > 0 && height > 0)
        {
            D3DVIEWPORT9 vp;
            ZeroMemory(&vp, sizeof(vp));
            vp.X      = (DWORD)max(x, 0);
            vp.Y      = (DWORD)max(y, 0);
            vp.Width  = (DWORD)width;
            vp.Height = (DWORD)height;
            vp.MinZ   = 0.0f;
            vp.MaxZ   = 1.0f;
            g_pd3dDevice->SetViewport(&vp);
        }

        if (ortho)
        {
            D3DXMATRIX orthoMat;
            ZeroMemory(&orthoMat, sizeof(orthoMat));
            orthoMat._11 =  2.0f / windowSetting.uDisplayWidth;
            orthoMat._14 = -1.0f;
            orthoMat._22 = -2.0f / windowSetting.uDisplayHeight;
            orthoMat._24 =  1.0f;
            orthoMat._33 =  1.0f / (FAR_PLANE - NEAR_PLANE);
            orthoMat._34 =  NEAR_PLANE / (NEAR_PLANE - FAR_PLANE);
            orthoMat._44 =  1.0f;

            g_pd3dDevice->SetVertexShaderConstantF(0, (float*)&orthoMat, 4);
        }
        else
        {
            float vp_x=(float)(xe_origx + x) / windowSetting.uDisplayWidth;
            float vp_y=(float)(xe_origy + y) / windowSetting.uDisplayHeight;
            float vp_w=(float)(xe_scalex * width) / windowSetting.uDisplayWidth;
            float vp_h=(float)(xe_scaley * height) / windowSetting.uDisplayHeight;

            float viewport_matrix[4][4] = {
                {vp_w,0,0,(vp_x*2+vp_w)-1},
                {0,vp_h,0,(vp_y*2+vp_h)-1},
                {0,0,1/(FAR_PLANE-NEAR_PLANE),NEAR_PLANE/(NEAR_PLANE-FAR_PLANE)},
                {0,0,0,1},
            };

            g_pd3dDevice->SetVertexShaderConstantF(0, (float*)viewport_matrix, 4);
        }
    }
}

void CxeRender::OneRTRVtx(u32 i)
{
    float depth = -(g_texRectTVtx[3].z*2-1);
    currentVertex->x=g_texRectTVtx[i].x;
    currentVertex->y=g_texRectTVtx[i].y;
    currentVertex->z=depth;
    currentVertex->w=1.0;
    currentVertex->color=MAKE_COLOR4(g_texRectTVtx[i].r,g_texRectTVtx[i].g,g_texRectTVtx[i].b,g_texRectTVtx[i].a);
    currentVertex->u0=g_texRectTVtx[i].tcord[0].u;
    currentVertex->v0=g_texRectTVtx[i].tcord[0].v;
    currentVertex->u1=g_texRectTVtx[i].tcord[1].u;
    currentVertex->v1=g_texRectTVtx[i].tcord[1].v;
    nextVertex();
}

void CxeRender::OneRFRVtx(u32 i, u32 j, u32 dwColor, float depth)
{
    u8 r = (u8)((dwColor>>16)&0xFF);
    u8 g = (u8)((dwColor>>8)&0xFF);
    u8 b = (u8)(dwColor&0xFF);
    u8 a = (u8)(dwColor>>24);
    currentVertex->x=m_fillRectVtx[i].x;
    currentVertex->y=m_fillRectVtx[j].y;
    currentVertex->z=depth;
    currentVertex->w=1.0;
    currentVertex->color=MAKE_COLOR4(r,g,b,a);
    currentVertex->u0=currentVertex->u1=currentVertex->v0=currentVertex->v1=0.0;
    nextVertex();
}

void CxeRender::OneDSRRVtx(u32 i)
{
    u8 r = (u8)(gRDP.fvPrimitiveColor[0]*255.0f);
    u8 g = (u8)(gRDP.fvPrimitiveColor[1]*255.0f);
    u8 b = (u8)(gRDP.fvPrimitiveColor[2]*255.0f);
    u8 a = (u8)(gRDP.fvPrimitiveColor[3]*255.0f);
    currentVertex->x=g_texRectTVtx[i].x;
    currentVertex->y=g_texRectTVtx[i].y;
    currentVertex->z=-g_texRectTVtx[i].z;
    currentVertex->w=1.0;
    currentVertex->color=MAKE_COLOR4(r,g,b,a);
    currentVertex->u0=g_texRectTVtx[i].tcord[0].u;
    currentVertex->v0=g_texRectTVtx[i].tcord[0].v;
    currentVertex->u1=g_texRectTVtx[i].tcord[1].u;
    currentVertex->v1=g_texRectTVtx[i].tcord[1].v;
    nextVertex();
}

bool CxeRender::RenderTexRect()
{
    glViewportWrapper(0, windowSetting.statusBarHeightToUse, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight, true);
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    {
        static int trrCount = 0;
        trrCount++;
        if (trrCount <= 10 || (trrCount % 100 == 0))
        {
            DWORD zEnable=0, zFunc=0, zWrite=0, aTest=0, aRef=0, aBlend=0, srcBlend=0, dstBlend=0, cull=0;
            g_pd3dDevice->GetRenderState(D3DRS_ZENABLE, &zEnable);
            g_pd3dDevice->GetRenderState(D3DRS_ZFUNC, &zFunc);
            g_pd3dDevice->GetRenderState(D3DRS_ZWRITEENABLE, &zWrite);
            g_pd3dDevice->GetRenderState(D3DRS_ALPHATESTENABLE, &aTest);
            g_pd3dDevice->GetRenderState(D3DRS_ALPHAREF, &aRef);
            g_pd3dDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &aBlend);
            g_pd3dDevice->GetRenderState(D3DRS_SRCBLEND, &srcBlend);
            g_pd3dDevice->GetRenderState(D3DRS_DESTBLEND, &dstBlend);
            g_pd3dDevice->GetRenderState(D3DRS_CULLMODE, &cull);
            char dbg3[512];
            sprintf(dbg3, "[d3d9-TEXRECT] #%d STATE: zEn=%d zFn=%d zWr=%d aTst=%d aRef=%d aBld=%d src=%d dst=%d cull=%d tex0=%p",
                trrCount, (int)zEnable, (int)zFunc, (int)zWrite,
                (int)aTest, (int)aRef, (int)aBlend, (int)srcBlend, (int)dstBlend, (int)cull,
                (void*)(m_curBoundTex[0] ? m_curBoundTex[0]->tex : NULL));
            DLOG(dbg3);
        }
    }
    if (g_pPS_FB) g_pd3dDevice->SetPixelShader(g_pPS_FB);
    BOOL useTex = TRUE;
    g_pd3dDevice->SetPixelShaderConstantB(0, &useTex, 1);
    OneRTRVtx(0); OneRTRVtx(1); OneRTRVtx(2);
    OneRTRVtx(0); OneRTRVtx(2); OneRTRVtx(3);
    drawVB();
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, xe_cull);
    return true;
}

bool CxeRender::RenderFillRect(uint32 dwColor, float depth)
{
    static int fillCount = 0;
    fillCount++;
    if (fillCount <= 20 || (fillCount % 100 == 0))
    {
        u8 r = (u8)((dwColor>>16)&0xFF);
        u8 g = (u8)((dwColor>>8)&0xFF);
        u8 b = (u8)(dwColor&0xFF);
        u8 a = (u8)(dwColor>>24);
        char dbg[256];
        sprintf(dbg, "[d3d9] RenderFillRect #%d: color=(%d,%d,%d,%d) depth=%.3f vtx0=(%.1f,%.1f) vtx1=(%.1f,%.1f)",
            fillCount, r, g, b, a, depth,
            m_fillRectVtx[0].x, m_fillRectVtx[0].y,
            m_fillRectVtx[1].x, m_fillRectVtx[1].y);
        DLOG(dbg);
    }

    glViewportWrapper(0, windowSetting.statusBarHeightToUse, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight, true);
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    if (g_pPS_FB) g_pd3dDevice->SetPixelShader(g_pPS_FB);
    BOOL useTex = FALSE;
    g_pd3dDevice->SetPixelShaderConstantB(0, &useTex, 1);
    OneRFRVtx(0,0,dwColor,depth);
    OneRFRVtx(1,0,dwColor,depth);
    OneRFRVtx(1,1,dwColor,depth);
    OneRFRVtx(0,0,dwColor,depth);
    OneRFRVtx(1,1,dwColor,depth);
    OneRFRVtx(0,1,dwColor,depth);
    drawVB();
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, xe_cull);
    return true;
}

bool CxeRender::RenderLine3D()
{
    ApplyZBias(0);
    ApplyZBias(m_dwZBias);
    return true;
}

bool CxeRender::RenderFlushTris()
{
    static int flushCount = 0;
    flushCount++;

    ApplyZBias(m_dwZBias);
    glViewportWrapper(windowSetting.vpLeftW, windowSetting.uDisplayHeight-windowSetting.vpTopW-windowSetting.vpHeightW+windowSetting.statusBarHeightToUse, windowSetting.vpWidthW, windowSetting.vpHeightW, false);

    u32 i;
    u32 nvr = gRSP.numVertices;

    if (flushCount <= 5 || (flushCount % 200 == 0))
    {
        char dbg[256];
        sprintf(dbg, "[d3d9] RenderFlushTris #%d: numVertices=%u viewport=(%d,%d,%d,%d)",
            flushCount, nvr,
            windowSetting.vpLeftW, windowSetting.vpTopW, windowSetting.vpWidthW, windowSetting.vpHeightW);
        DLOG(dbg);
        if (nvr >= 3 && nvr <= 1000)
        {
            int vi0 = g_vtxIndex[0];
            int vi1 = g_vtxIndex[1];
            int vi2 = g_vtxIndex[2];
            float x0 = g_vtxProjected5[vi0][0] / g_vtxProjected5[vi0][3];
            float y0 = g_vtxProjected5[vi0][1] / g_vtxProjected5[vi0][3];
            float x1 = g_vtxProjected5[vi1][0] / g_vtxProjected5[vi1][3];
            float y1 = g_vtxProjected5[vi1][1] / g_vtxProjected5[vi1][3];
            float x2 = g_vtxProjected5[vi2][0] / g_vtxProjected5[vi2][3];
            float y2 = g_vtxProjected5[vi2][1] / g_vtxProjected5[vi2][3];
            // Area con signo del triangulo en NDC (x,y ya divididos por w).
            // Positivo = CCW en un sistema Y-arriba estandar; negativo = CW.
            float signedArea = (x1-x0)*(y2-y0) - (x2-x0)*(y1-y0);
            sprintf(dbg, "[d3d9]   tri: v0=(%.3f,%.3f) v1=(%.3f,%.3f) v2=(%.3f,%.3f) signedArea=%.6f (%s)",
                x0, y0, x1, y1, x2, y2, signedArea, signedArea > 0 ? "CCW" : "CW");
            DLOG(dbg);
        }
    }

    for (i=0; i<nvr; ++i)
    {
        int vi = g_vtxIndex[i];
        float zoffset = m_bPolyOffset ? 0.999f : 1.0f;
        currentVertex->x = g_vtxProjected5[vi][0];
        currentVertex->y = g_vtxProjected5[vi][1];
        currentVertex->z = g_vtxProjected5[vi][2]*zoffset;
        currentVertex->w = g_vtxProjected5[vi][3];
        currentVertex->color = MAKE_COLOR4(g_oglVtxColors[vi][0],g_oglVtxColors[vi][1],g_oglVtxColors[vi][2],g_oglVtxColors[vi][3]);
        currentVertex->u0 = g_vtxBuffer[i].tcord[0].u;
        currentVertex->v0 = g_vtxBuffer[i].tcord[0].v;
        currentVertex->u1 = g_vtxBuffer[i].tcord[1].u;
        currentVertex->v1 = g_vtxBuffer[i].tcord[1].v;
        nextVertex();
    }

    if (flushCount <= 5 || (flushCount % 200 == 0))
    {
        DWORD zEnable=0, zFunc=0, zWrite=0, aTest=0, aRef=0, aBlend=0, srcBlend=0, dstBlend=0, cull=0;
        IDirect3DPixelShader9 * curPS = NULL;
        g_pd3dDevice->GetRenderState(D3DRS_ZENABLE, &zEnable);
        g_pd3dDevice->GetRenderState(D3DRS_ZFUNC, &zFunc);
        g_pd3dDevice->GetRenderState(D3DRS_ZWRITEENABLE, &zWrite);
        g_pd3dDevice->GetRenderState(D3DRS_ALPHATESTENABLE, &aTest);
        g_pd3dDevice->GetRenderState(D3DRS_ALPHAREF, &aRef);
        g_pd3dDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &aBlend);
        g_pd3dDevice->GetRenderState(D3DRS_SRCBLEND, &srcBlend);
        g_pd3dDevice->GetRenderState(D3DRS_DESTBLEND, &dstBlend);
        g_pd3dDevice->GetRenderState(D3DRS_CULLMODE, &cull);
        g_pd3dDevice->GetPixelShader(&curPS);
        char dbg[512];
        sprintf(dbg, "[d3d9]   STATE: zEn=%d zFn=%d zWr=%d aTst=%d aRef=%d aBld=%d src=%d dst=%d cull=%d tex0=%p tex1=%p ps=%p vtx=%d",
            (int)zEnable, (int)zFunc, (int)zWrite,
            (int)aTest, (int)aRef, (int)aBlend, (int)srcBlend, (int)dstBlend, (int)cull,
            (void*)(m_curBoundTex[0] ? m_curBoundTex[0]->tex : NULL),
            (void*)(m_curBoundTex[1] ? m_curBoundTex[1]->tex : NULL),
            (void*)curPS,
            (int)nvr);
        DLOG(dbg);
        if (curPS) curPS->Release();
        if (nvr > 0)
        {
            int vi0 = g_vtxIndex[0];
            char dbgv[256];
            sprintf(dbgv, "[d3d9]   VTX0: color=0x%08X (R=%d G=%d B=%d A=%d) uv=(%.3f,%.3f)",
                MAKE_COLOR4(g_oglVtxColors[vi0][0],g_oglVtxColors[vi0][1],g_oglVtxColors[vi0][2],g_oglVtxColors[vi0][3]),
                g_oglVtxColors[vi0][0], g_oglVtxColors[vi0][1], g_oglVtxColors[vi0][2], g_oglVtxColors[vi0][3],
                g_vtxBuffer[0].tcord[0].u, g_vtxBuffer[0].tcord[0].v);
            DLOG(dbgv);
        }
    }

    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, xe_cull);
    drawVB();
    return true;
}

void CxeRender::OneDSTVtx(u32 i)
{
    u8 r = (u8)((g_texRectTVtx[i].dcDiffuse>>16)&0xFF);
    u8 g = (u8)((g_texRectTVtx[i].dcDiffuse>>8)&0xFF);
    u8 b = (u8)(g_texRectTVtx[i].dcDiffuse&0xFF);
    u8 a = (u8)(g_texRectTVtx[i].dcDiffuse>>24);

    currentVertex->x=g_texRectTVtx[i].x;
    currentVertex->y=g_texRectTVtx[i].y;
    currentVertex->z=-g_texRectTVtx[i].z;
    currentVertex->w=1.0f;
    currentVertex->color=MAKE_COLOR4(r,g,b,a);
    currentVertex->u0=g_texRectTVtx[i].tcord[0].u;
    currentVertex->v0=g_texRectTVtx[i].tcord[0].v;
    currentVertex->u1=g_texRectTVtx[i].tcord[1].u;
    currentVertex->v1=g_texRectTVtx[i].tcord[1].v;
    nextVertex();
}

void CxeRender::DrawSimple2DTexture(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, COLOR dif, COLOR spe, float z, float rhw)
{
    StartDrawSimple2DTexture(x0, y0, x1, y1, u0, v0, u1, v1, dif, spe, z, rhw);
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    glViewportWrapper(0, windowSetting.statusBarHeightToUse, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight, true);
    OneDSTVtx(0); OneDSTVtx(1); OneDSTVtx(2);
    OneDSTVtx(0); OneDSTVtx(2); OneDSTVtx(3);
    drawVB();
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, xe_cull);
}

void CxeRender::DrawText(const char* str, RECT *rect)
{
    // El original tampoco implementa esto (assert(false) en xenos_backend.cpp);
    // se deja como no-op para no colgar el build en Xbox 360.
}

void CxeRender::DrawSpriteR_Render()    // Con rotacion
{
    glViewportWrapper(0, windowSetting.statusBarHeightToUse, windowSetting.uDisplayWidth, windowSetting.uDisplayHeight, true);
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    OneDSRRVtx(0); OneDSRRVtx(1); OneDSRRVtx(2);
    OneDSRRVtx(0); OneDSRRVtx(2); OneDSRRVtx(3);
    drawVB();
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, xe_cull);
}

void CxeRender::DrawObjBGCopy(uObjBg &info)
{
    CRender::LoadObjBGCopy(info);
    CRender::DrawObjBGCopy(info);
}





// ---------------- CxeBlender ----------------

void CxeBlender::NormalAlphaBlender(void)
{
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
}

void CxeBlender::DisableAlphaBlender(void)
{
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
}

// BLEND_* (1-based, winlnxdefs.h) -> D3DBLEND (Xbox 360 XDK, 0-based with gaps)
static const DWORD d3d9BlendMap[] = {
    D3DBLEND_SRCALPHA,          // index 0 = default/fallback (matches Xenos XE_BLEND_SRCALPHA)
    D3DBLEND_ZERO,              // BLEND_ZERO = 1
    D3DBLEND_ONE,               // BLEND_ONE = 2
    D3DBLEND_SRCCOLOR,          // BLEND_SRCCOLOR = 3
    D3DBLEND_INVSRCCOLOR,       // BLEND_INVSRCCOLOR = 4
    D3DBLEND_SRCALPHA,          // BLEND_SRCALPHA = 5
    D3DBLEND_INVSRCALPHA,       // BLEND_INVSRCALPHA = 6
    D3DBLEND_DESTALPHA,         // BLEND_DESTALPHA = 7
    D3DBLEND_INVDESTALPHA,      // BLEND_INVDESTALPHA = 8
    D3DBLEND_DESTCOLOR,         // BLEND_DESTCOLOR = 9
    D3DBLEND_INVDESTCOLOR,      // BLEND_INVDESTCOLOR = 10
    D3DBLEND_SRCALPHASAT,       // BLEND_SRCALPHASAT = 11
    D3DBLEND_SRCALPHASAT,       // BLEND_BOTHSRCALPHA = 12 (unsupported, fallback)
    D3DBLEND_SRCALPHASAT,       // BLEND_BOTHINVSRCALPHA = 13 (unsupported, fallback)
    D3DBLEND_BLENDFACTOR,       // BLEND_BLENDFACTOR = 14
    D3DBLEND_INVBLENDFACTOR,    // BLEND_INVBLENDFACTOR = 15
};

void CxeBlender::BlendFunc(uint32 srcFunc, uint32 desFunc)
{
    blend_src = srcFunc;
    blend_dst = desFunc;
    DWORD d3dSrc = (srcFunc < sizeof(d3d9BlendMap)/sizeof(d3d9BlendMap[0])) ? d3d9BlendMap[srcFunc] : D3DBLEND_ONE;
    DWORD d3dDst = (desFunc < sizeof(d3d9BlendMap)/sizeof(d3d9BlendMap[0])) ? d3d9BlendMap[desFunc] : D3DBLEND_ZERO;
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, d3dSrc);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, d3dDst);
}

void CxeBlender::Enable() { g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE); }
void CxeBlender::Disable() { g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE); }

// ---------------- CxeTexture ----------------

CxeTexture::CxeTexture(uint32 dwWidth, uint32 dwHeight, TextureUsage usage)
    : CTexture(dwWidth, dwHeight, usage), tex(NULL), indirect(false)
{
    static int texCount = 0;
    // D3DFMT_LIN_* fuerza layout LINEAL en vez del tileado/swizzled por
    // defecto de Xenos. Sin esto, LockRect+memcpy lineal (como hace Rice
    // al subir texturas decodificadas del N64) produce el clasico
    // resultado en "bloques"/pixelado, porque el hardware interpreta la
    // memoria como si estuviera tileada cuando en realidad no lo esta.
    HRESULT hr = g_pd3dDevice->CreateTexture(dwWidth, dwHeight, 1, 0,
        D3DFMT_LIN_A8R8G8B8, 0, &tex, NULL);
    if (FAILED(hr))
    {
        char dbgbuf8[256]; sprintf(dbgbuf8, "[d3d9] CreateTexture FALLO (%ux%u, hr=0x%08X)\n",
            (unsigned int)dwWidth, (unsigned int)dwHeight, (unsigned int)hr); DLOG(dbgbuf8);
        tex = NULL;
    }
    else
    {
        m_pTexture = (LPRICETEXTURE)this;
        texCount++;
        char dbg[256]; sprintf(dbg, "[d3d9] CreateTexture #%d OK %ux%u tex=%p",
            texCount, (unsigned int)dwWidth, (unsigned int)dwHeight, (void*)tex);
        DLOG(dbg);
    }
}

CxeTexture::~CxeTexture()
{
    if (tex) { tex->Release(); tex = NULL; } // equivalente a Xe_DestroyTexture
}
CxeRenderTexture::CxeRenderTexture(int width, int height, RenderTextureInfo* pInfo, TextureUsage usage)
    : CRenderTexture(width, height, pInfo, usage)
    , m_pD3DTexture(NULL), m_pRTSurface(NULL), m_pOldRTSurface(NULL), m_pOldDSSurface(NULL)
    , m_pResolvedTexture(NULL), m_pResolvedSurface(NULL)
{
    m_width = width;
    m_height = height;

    HRESULT hr = g_pd3dDevice->CreateTexture(width, height, 1,
        D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_pD3DTexture, NULL);
    if (FAILED(hr))
    {
        char dbg[256]; sprintf(dbg, "[d3d9] CxeRenderTexture CreateTexture FALLO (%dx%d, hr=0x%08X)", width, height, (unsigned int)hr);
        DLOG(dbg);
        m_pD3DTexture = NULL;
        return;
    }
    m_pD3DTexture->GetSurfaceLevel(0, &m_pRTSurface);

    // Textura lineal separada: destino del Resolve. Esta es la que se
    // muestrea despues, nunca m_pD3DTexture directamente (esa queda
    // tileada por EDRAM y produce el patron de rayas si se lee como lineal).
    HRESULT hrResolved = g_pd3dDevice->CreateTexture(width, height, 1,
        0, D3DFMT_LIN_A8R8G8B8, D3DPOOL_DEFAULT, &m_pResolvedTexture, NULL);
    if (FAILED(hrResolved))
    {
        char dbg[256]; sprintf(dbg, "[d3d9] CxeRenderTexture CreateTexture(resolved) FALLO (%dx%d, hr=0x%08X)", width, height, (unsigned int)hrResolved);
        DLOG(dbg);
        m_pResolvedTexture = NULL;
    }
    else
    {
        m_pResolvedTexture->GetSurfaceLevel(0, &m_pResolvedSurface);
    }

    // Envolver en un CxeTexture para que el resto de Rice (SetCurrentTexture,
    // sampling en el shader) lo trate como cualquier otra textura normal.
    CxeTexture* wrapper = new CxeTexture(width, height, usage);
    if (wrapper->tex) wrapper->tex->Release();
    // El wrapper apunta a la textura RESUELTA (lineal), no al render target crudo.
    wrapper->tex = m_pResolvedTexture ? m_pResolvedTexture : m_pD3DTexture;
    if (wrapper->tex) wrapper->tex->AddRef();
    m_pTexture = wrapper;

    char dbg[256]; sprintf(dbg, "[d3d9] CxeRenderTexture creada OK %dx%d tex=%p surf=%p resolved=%p", width, height, (void*)m_pD3DTexture, (void*)m_pRTSurface, (void*)m_pResolvedTexture);
    DLOG(dbg);
}
CxeRenderTexture::~CxeRenderTexture()
{
    if (m_pResolvedSurface) { m_pResolvedSurface->Release(); m_pResolvedSurface = NULL; }
    if (m_pResolvedTexture) { m_pResolvedTexture->Release(); m_pResolvedTexture = NULL; }
    if (m_pRTSurface)  { m_pRTSurface->Release();  m_pRTSurface = NULL; }
    if (m_pD3DTexture) { m_pD3DTexture->Release();  m_pD3DTexture = NULL; }
    if (m_pTexture) { delete m_pTexture; m_pTexture = NULL; }
}
bool CxeRenderTexture::SetAsRenderTarget(bool enable)
{
    if (!g_pd3dDevice) return false;

    if (enable)
    {
        if (!m_pRTSurface) return false;
        g_pd3dDevice->GetRenderTarget(0, &m_pOldRTSurface);
        g_pd3dDevice->GetDepthStencilSurface(&m_pOldDSSurface);
        HRESULT hr = g_pd3dDevice->SetRenderTarget(0, m_pRTSurface);
        m_beingRendered = SUCCEEDED(hr);
        char dbg[256]; sprintf(dbg, "[d3d9] SetAsRenderTarget(true) hr=0x%08X", (unsigned int)hr);
        DLOG(dbg);
        return m_beingRendered;
    }
    else
    {
        // Antes de restaurar el render target anterior, resolver el
        // contenido tileado (m_pRTSurface) a la textura lineal que se
        // va a muestrear despues (m_pResolvedSurface).
        if (m_pRTSurface && m_pResolvedSurface)
        {
            HRESULT hrResolve = g_pd3dDevice->Resolve(D3DRESOLVE_RENDERTARGET0, NULL,
                m_pResolvedTexture, NULL, 0, 0, NULL, 0.0f, 0, NULL);
            char dbgr[256]; sprintf(dbgr, "[d3d9] CxeRenderTexture Resolve hr=0x%08X", (unsigned int)hrResolve);
            DLOG(dbgr);
        }
        if (m_pOldRTSurface)
        {
            g_pd3dDevice->SetRenderTarget(0, m_pOldRTSurface);
            m_pOldRTSurface->Release();
            m_pOldRTSurface = NULL;
        }
        if (m_pOldDSSurface)
        {
            g_pd3dDevice->SetDepthStencilSurface(m_pOldDSSurface);
            m_pOldDSSurface->Release();
            m_pOldDSSurface = NULL;
        }
        m_beingRendered = false;
        return true;
    }
}
void CxeRenderTexture::LoadTexture(TxtrCacheEntry* pEntry)
{
    if (!pEntry || !pEntry->pTexture)
        return;

    // Si esta siendo renderizado, parar temporalmente para poder leer
    bool wasBeingRendered = m_beingRendered;
    if (wasBeingRendered)
    {
        SetAsRenderTarget(false);
    }

    // La fuente es la textura resolved (lineal, ya resuelta del EDRAM)
    CxeTexture* pSrcTex = (CxeTexture*)m_pTexture;
    IDirect3DTexture9* pSrcD3DTex = m_pResolvedTexture ? m_pResolvedTexture : (pSrcTex ? pSrcTex->tex : NULL);
    if (!pSrcD3DTex)
    {
        if (wasBeingRendered) SetAsRenderTarget(true);
        return;
    }

    // El destino es la textura del cache entry
    CxeTexture* pDstTex = (CxeTexture*)pEntry->pTexture;
    if (!pDstTex || !pDstTex->tex)
    {
        if (wasBeingRendered) SetAsRenderTarget(true);
        return;
    }

    IDirect3DSurface9* pSrcSurface = NULL;
    IDirect3DSurface9* pDstSurface = NULL;
    pSrcD3DTex->GetSurfaceLevel(0, &pSrcSurface);
    pDstTex->tex->GetSurfaceLevel(0, &pDstSurface);

    if (pSrcSurface && pDstSurface)
    {
        // Calcular la sub-rect fuente basada en la diferencia de direcciones
        TxtrInfo &ti = pEntry->ti;
        int left = (int)(ti.Address - m_pInfo->CI_Info.dwAddr) % (int)m_pInfo->CI_Info.bpl + (int)ti.LeftToLoad;
        int top  = (int)(ti.Address - m_pInfo->CI_Info.dwAddr) / (int)m_pInfo->CI_Info.bpl + (int)ti.TopToLoad;

        if (left >= 0 && top >= 0 && left < m_width && top < m_height)
        {
            RECT srcrect = {
                (LONG)(left * m_pInfo->scaleX),
                (LONG)(top * m_pInfo->scaleY),
                (LONG)(min(m_width, left + (int)ti.WidthToLoad) * m_pInfo->scaleX),
                (LONG)(min(m_height, top + (int)ti.HeightToLoad) * m_pInfo->scaleY)
            };
            RECT dstrect = { 0, 0, (LONG)ti.WidthToLoad, (LONG)ti.HeightToLoad };

            HRESULT hr = D3DXLoadSurfaceFromSurface(pDstSurface, NULL, &dstrect,
                pSrcSurface, NULL, &srcrect, D3DX_FILTER_POINT, 0xFF000000);
            if (FAILED(hr))
            {
                char dbg[256]; sprintf(dbg, "[d3d9] LoadTexture D3DXLoadSurfaceFromSurface FAILED hr=0x%08X", (unsigned int)hr);
                DLOG(dbg);
            }
        }
    }

    if (pSrcSurface) pSrcSurface->Release();
    if (pDstSurface) pDstSurface->Release();

    // Actualizar variables de la textura destino
    pEntry->pTexture->SetOthersVariables();

    if (wasBeingRendered)
    {
        SetAsRenderTarget(true);
    }
}
void CxeRenderTexture::StoreToRDRAM(int infoIdx)
{
    if (!gRenderTextureInfos || infoIdx < 0 || infoIdx >= numOfTxtBufInfos)
        return;
    if (!frameBufferOptions.bRenderTextureWriteBack)
        return;

    RenderTextureInfo &info = gRenderTextureInfos[infoIdx];
    if (!m_pResolvedSurface || !m_pResolvedTexture)
        return;

    // Crear staging texture en system memory una sola vez (estatica)
    // Usar D3DFMT_LIN_A8R8G8B8 para coincidir con m_pResolvedTexture
    static IDirect3DTexture9* s_pStaging = NULL;
    static int s_stagW = 0, s_stagH = 0;
    if (!s_pStaging || s_stagW != m_width || s_stagH != m_height)
    {
        if (s_pStaging) { s_pStaging->Release(); s_pStaging = NULL; }
        HRESULT hr = g_pd3dDevice->CreateTexture(m_width, m_height, 1, 0,
            D3DFMT_LIN_A8R8G8B8, D3DPOOL_SYSTEMMEM, &s_pStaging, NULL);
        if (FAILED(hr)) return;
        s_stagW = m_width;
        s_stagH = m_height;
    }

    IDirect3DSurface9* pStagingSurface = NULL;
    s_pStaging->GetSurfaceLevel(0, &pStagingSurface);
    if (!pStagingSurface) return;

    // Copiar desde el resolved surface al staging (maneja conversion de formato)
    RECT srcRect = {0, 0, m_width, m_height};
    RECT dstRect = {0, 0, m_width, m_height};
    HRESULT hrCopy = D3DXLoadSurfaceFromSurface(pStagingSurface, NULL, &dstRect,
        m_pResolvedSurface, NULL, &srcRect, D3DX_FILTER_POINT, 0xFF000000);
    if (FAILED(hrCopy))
    {
        pStagingSurface->Release();
        return;
    }

    // Lock y leer pixeles
    D3DLOCKED_RECT lockedRect;
    HRESULT hrLock = s_pStaging->LockRect(0, &lockedRect, NULL, D3DLOCK_READONLY);
    if (FAILED(hrLock))
    {
        pStagingSurface->Release();
        return;
    }

    uint32 fmt = info.CI_Info.dwFormat;
    uint32 siz = info.CI_Info.dwSize;
    uint32 addr = info.CI_Info.dwAddr;
    uint32 width = info.N64Width;
    uint32 height = info.knownHeight ? info.N64Height : info.maxUsedHeight;
    if (height == 0) height = m_height;
    if (width == 0) width = m_width;

    uint8* pSrc = (uint8*)lockedRect.pBits;
    uint32 srcPitch = lockedRect.Pitch;

    if (siz == TXT_SIZE_16b && fmt == TXT_FMT_RGBA)
    {
        // RGBA16 (IA16/CI16 handled as RGBA16 on GPU)
        uint16* pDst = (uint16*)(g_pRDRAMu8 + addr);
        uint32 copyWidth = min(width, (uint32)m_width);
        uint32 copyHeight = min(height, (uint32)m_height);
        for (uint32 y = 0; y < copyHeight; y++)
        {
            uint8* pS = pSrc + y * srcPitch;
            for (uint32 x = 0; x < copyWidth; x++)
            {
                // A8R8G8B8 en memoria: [B, G, R, A] (little-endian Xbox 360)
                uint32 pixel = *(uint32*)(pS + x * 4);
                uint8 r = (uint8)(pixel & 0xFF);
                uint8 g = (uint8)((pixel >> 8) & 0xFF);
                uint8 b = (uint8)((pixel >> 16) & 0xFF);
                uint8 a = (uint8)((pixel >> 24) & 0xFF);
                pDst[x ^ S16] = ConvertRGBATo555(r, g, b, a);
            }
            pDst += width;
        }
    }
    else if (siz == TXT_SIZE_32b)
    {
        // RGBA32
        uint32* pDst = (uint32*)(g_pRDRAMu8 + addr);
        uint32 copyWidth = min(width, (uint32)m_width);
        uint32 copyHeight = min(height, (uint32)m_height);
        for (uint32 y = 0; y < copyHeight; y++)
        {
            uint8* pS = pSrc + y * srcPitch;
            for (uint32 x = 0; x < copyWidth; x++)
            {
                uint32 pixel = *(uint32*)(pS + x * 4);
                uint8 r = (uint8)(pixel & 0xFF);
                uint8 g = (uint8)((pixel >> 8) & 0xFF);
                uint8 b = (uint8)((pixel >> 16) & 0xFF);
                uint8 a = (uint8)((pixel >> 24) & 0xFF);
                // N64 RGBA32: R,G,B,A byte order en memoria (big-endian = A,B,G,R en 32-bit)
                pDst[x] = ((uint32)a << 24) | ((uint32)b << 16) | ((uint32)g << 8) | (uint32)r;
            }
            pDst += width;
        }
    }
    else if (siz == TXT_SIZE_8b && fmt == TXT_FMT_I)
    {
        // Intensity 8b
        uint8* pDst = g_pRDRAMu8 + addr;
        uint32 copyWidth = min(width, (uint32)m_width);
        uint32 copyHeight = min(height, (uint32)m_height);
        for (uint32 y = 0; y < copyHeight; y++)
        {
            uint8* pS = pSrc + y * srcPitch;
            for (uint32 x = 0; x < copyWidth; x++)
            {
                uint32 pixel = *(uint32*)(pS + x * 4);
                uint8 r = (uint8)(pixel & 0xFF);
                uint8 g = (uint8)((pixel >> 8) & 0xFF);
                uint8 b = (uint8)((pixel >> 16) & 0xFF);
                pDst[x ^ S8] = (uint8)((r + g + b) / 3);
            }
            pDst += width;
        }
    }
    else if (siz == TXT_SIZE_8b && fmt == TXT_FMT_CI)
    {
        // CI8: convertir a RGBA1555 y buscar en TLUT
        uint8* pDst = g_pRDRAMu8 + addr;
        uint32 copyWidth = min(width, (uint32)m_width);
        uint32 copyHeight = min(height, (uint32)m_height);
        InitTlutReverseLookup();
        for (uint32 y = 0; y < copyHeight; y++)
        {
            uint8* pS = pSrc + y * srcPitch;
            for (uint32 x = 0; x < copyWidth; x++)
            {
                uint32 pixel = *(uint32*)(pS + x * 4);
                uint8 r = (uint8)(pixel & 0xFF);
                uint8 g = (uint8)((pixel >> 8) & 0xFF);
                uint8 b = (uint8)((pixel >> 16) & 0xFF);
                uint8 a = (uint8)((pixel >> 24) & 0xFF);
                uint16 c = ConvertRGBATo555(r, g, b, a);
                pDst[x ^ S8] = RevTlutTable[c];
            }
            pDst += width;
        }
    }

    s_pStaging->UnlockRect(0);
    pStagingSurface->Release();

    {
        static int stCount = 0;
        stCount++;
        if (stCount <= 10 || (stCount % 50 == 0))
        {
            char dbg[256]; sprintf(dbg, "[d3d9] StoreToRDRAM #%d idx=%d addr=0x%08X %ux%u fmt=%u siz=%u",
                stCount, infoIdx, addr, width, height, fmt, siz);
            DLOG(dbg);
        }
    }
}

bool CxeTexture::StartUpdate(DrawInfo *di)
{
    if (!tex) return false;

    D3DLOCKED_RECT lockedRect;
    HRESULT hr = tex->LockRect(0, &lockedRect, NULL, 0);
    if (FAILED(hr))
    {
        char dbgbuf9[256]; sprintf(dbgbuf9, "[d3d9_backend] LockRect fallo (hr=0x%08X)\n", (unsigned int)hr); OutputDebugStringA(dbgbuf9);
        return false;
    }

    di->dwWidth         = (unsigned short)m_dwWidth;
    di->dwHeight        = (unsigned short)m_dwHeight;
    di->dwCreatedWidth  = (unsigned short)m_dwCreatedTextureWidth;
    di->dwCreatedHeight = (unsigned short)m_dwCreatedTextureHeight;
    di->lPitch          = lockedRect.Pitch;
    di->lpSurface       = lockedRect.pBits;
    {
        static int suCount = 0;
        suCount++;
        {
            char dbgsu[256]; sprintf(dbgsu, "[d3d9] StartUpdate #%d w=%u h=%u pitch=%d surf=%p",
                suCount, (unsigned int)m_dwWidth, (unsigned int)m_dwHeight, lockedRect.Pitch, lockedRect.pBits);
            DLOG(dbgsu);
        }
    }

    return true;
}

void CxeTexture::EndUpdate(DrawInfo *di)
{
    (void)di;
    if (tex) tex->UnlockRect(0);
}

// ---------------- CxeColorCombiner ----------------

CxeColorCombiner::CxeColorCombiner(CRender *pRender)
    : CColorCombiner(pRender), m_lastIndex(-1), m_dwLastMux0(0), m_dwLastMux1(0)
{
    m_pxeRender = (CxeRender*)pRender;
    if (m_pDecodedMux) delete m_pDecodedMux;
    m_pDecodedMux = new DecodedMuxForPixelShader;
}
CxeColorCombiner::~CxeColorCombiner() {}

bool CxeColorCombiner::Initialize(void) { return true; }
void CxeColorCombiner::InitCombinerBlenderForSimpleTextureDraw(uint32 tile)
{
    BOOL useTex = TRUE;
    g_pd3dDevice->SetPixelShaderConstantB(0, &useTex, 1);
    DisableCombiner();

    if (g_textures[tile].m_pCxeTexture)
    {
        m_pxeRender->EnableTexUnit(0, TRUE);

        g_pd3dDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        g_pd3dDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

        g_pd3dDevice->SetTexture(0, g_textures[tile].m_pCxeTexture->tex);
        m_pxeRender->m_curBoundTex[0] = g_textures[tile].m_pCxeTexture;
    }
    m_pxeRender->SetAllTexelRepeatFlag();
    m_pxeRender->SetAlphaTestEnable(FALSE);
}
void CxeColorCombiner::DisableCombiner(void)
{
    g_pd3dDevice->SetPixelShader(g_pPS_FB);
    m_lastIndex = -1;
}

void CxeColorCombiner::InitCombinerCycleCopy(void)
{
    BOOL useTex = TRUE;
    g_pd3dDevice->SetPixelShaderConstantB(0, &useTex, 1);
    DisableCombiner();

    m_pxeRender->EnableTexUnit(0, TRUE);
    CxeTexture* pTexture = g_textures[gRSP.curTile].m_pCxeTexture;
    if (pTexture)
    {
        m_pxeRender->BindTexture(pTexture, 0);
        m_pxeRender->SetTexelRepeatFlags(gRSP.curTile);
    }
}

void CxeColorCombiner::InitCombinerCycleFill(void)
{
    BOOL useTex = FALSE;
    g_pd3dDevice->SetPixelShaderConstantB(0, &useTex, 1);
    DisableCombiner();

    for (int i = 0; i < m_supportedStages; i++)
    {
        m_pxeRender->EnableTexUnit(i, FALSE);
    }
}

void CxeColorCombiner::InitCombinerCycle12(void)
{
    bool combinerIsChanged = false;

    if (m_pDecodedMux->m_dwMux0 != m_dwLastMux0 || m_pDecodedMux->m_dwMux1 != m_dwLastMux1 || m_lastIndex < 0)
    {
        combinerIsChanged = true;
        m_lastIndex = FindCompiledMux();
        if (m_lastIndex < 0)
        {
            m_lastIndex = ParseDecodedMux();
        }
        m_dwLastMux0 = m_pDecodedMux->m_dwMux0;
        m_dwLastMux1 = m_pDecodedMux->m_dwMux1;
    }

    if (m_bCycleChanged || combinerIsChanged || gRDP.texturesAreReloaded || gRDP.colorsAreReloaded)
    {
        if (m_bCycleChanged || combinerIsChanged)
        {
            GenerateCombinerSettingConstants(m_lastIndex);
            GenerateCombinerSetting(m_lastIndex);
        }
        else if (gRDP.colorsAreReloaded)
        {
            GenerateCombinerSettingConstants(m_lastIndex);
        }

        m_pxeRender->SetAllTexelRepeatFlag();

        gRDP.colorsAreReloaded = false;
        gRDP.texturesAreReloaded = false;
    }
    else
    {
        m_pxeRender->SetAllTexelRepeatFlag();
    }
}

int CxeColorCombiner::ParseDecodedMux()
{
    xeShaderCombinerSaveType res;

    res.shader = GenerateShader();
    res.dwMux0 = m_pDecodedMux->m_dwMux0;
    res.dwMux1 = m_pDecodedMux->m_dwMux1;
    res.fogIsUsed = gRDP.bFogEnableInBlender && gRSP.bFogEnabled;

    m_vCompiledShaders.push_back(res);
    m_lastIndex = m_vCompiledShaders.size() - 1;

    return m_lastIndex;
}

// Tabla de fuentes del combiner, equivalente a muxToReg_Map del original
struct MuxSrc { const char * expr; const char * expr_a; };

static const MuxSrc muxSrcTable[] = {
    { "cst0",       "cst0.w" },
    { "cst1",       "cst1.w" },
    { "comb",       "comb.w" },
    { "tex0v",      "tex0v.w" },
    { "tex1v",      "tex1v.w" },
    { "prim",       "prim.w" },
    { "shade",      "shade.w" },
    { "envc",       "envc.w" },
    { "comb.w",     "comb.w" },
    { "tex0v.w",    "tex0v.w" },
    { "tex1v.w",    "tex1v.w" },
    { "prim.w",     "prim.w" },
    { "shade.w",    "shade.w" },
    { "envc.w",     "envc.w" },
    { "lodfrac",    "lodfrac.w" },
    { "primlodfrac","primlodfrac.w" },
    { "cst1",       "cst1.w" },
    { "cst1",       "cst1.w" },
};

static const char * muxSrc(uint8 val, bool alphaChannel)
{
    int idx = val & 0x1F;
    if (idx >= 18) idx = 0;
    return alphaChannel ? muxSrcTable[idx].expr_a : muxSrcTable[idx].expr;
}

static void appendCombinerExpr(char * out, size_t outSize, CombinerFormatType spl,
                                const char * a, const char * b, const char * c, const char * d)
{
    char tmp[256];
    switch (spl)
    {
        case CM_FMT_TYPE_NOT_USED:
            sprintf(tmp, "float4(0,0,0,0)");
            break;
        case CM_FMT_TYPE_D:
            sprintf(tmp, "(%s)", d);
            break;
        case CM_FMT_TYPE_A_MOD_C:
            sprintf(tmp, "((%s)*(%s))", a, c);
            break;
        case CM_FMT_TYPE_A_ADD_D:
            sprintf(tmp, "saturate((%s)+(%s))", a, d);
            break;
        case CM_FMT_TYPE_A_SUB_B:
            sprintf(tmp, "((%s)-(%s))", a, b);
            break;
        case CM_FMT_TYPE_A_MOD_C_ADD_D:
            sprintf(tmp, "saturate((%s)*(%s)+(%s))", a, c, d);
            break;
        case CM_FMT_TYPE_A_LERP_B_C:
            sprintf(tmp, "lerp((%s),(%s),(%s))", b, a, c);
            break;
        case CM_FMT_TYPE_A_SUB_B_ADD_D:
            sprintf(tmp, "saturate(((%s)-(%s))+(%s))", a, b, d);
            break;
        case CM_FMT_TYPE_A_SUB_B_MOD_C:
            sprintf(tmp, "(((%s)-(%s))*(%s))", a, b, c);
            break;
        case CM_FMT_TYPE_A_ADD_B_MOD_C:
            sprintf(tmp, "saturate(((%s)+(%s))*(%s))", a, b, c);
            break;
        case CM_FMT_TYPE_A_B_C_D:
        case CM_FMT_TYPE_A_B_C_A:
            sprintf(tmp, "saturate(((%s)-(%s))*(%s)+(%s))", a, b, c, (spl==CM_FMT_TYPE_A_B_C_A)?a:d);
            break;
        default:
            sprintf(tmp, "float4(0,0,0,0)");
            break;
    }
    strncat(out, tmp, outSize - strlen(out) - 1);
}

IDirect3DPixelShader9 * CxeColorCombiner::GenerateShader()
{
    DecodedMuxForPixelShader &mux = *(DecodedMuxForPixelShader*)m_pDecodedMux;

    mux.splitType[0] = mux.splitType[1] = mux.splitType[2] = mux.splitType[3] = CM_FMT_TYPE_NOT_CHECKED;
    m_pDecodedMux->Reformat(false);

    char hlsl[4096];
    strcpy(hlsl,
        "sampler tex0 : register(s0);\n"
        "sampler tex1 : register(s1);\n"
        "float4 cst0 : register(c0);\n"
        "float4 cst1 : register(c1);\n"
        "float4 prim : register(c2);\n"
        "float4 envc : register(c3);\n"
        "float4 lodfrac : register(c4);\n"
        "float4 primlodfrac : register(c5);\n"
        "struct PS_IN { float2 uv0:TEXCOORD0; float2 uv1:TEXCOORD1; float4 shade:COLOR; };\n"
        "float4 main(PS_IN IN) : COLOR\n"
        "{\n"
        "    float4 tex0v = tex2D(tex0, IN.uv0);\n"
        "    float4 tex1v = tex2D(tex1, IN.uv1);\n"
        "    float4 shade = IN.shade;\n"
        "    float4 comb = float4(0,0,0,0);\n"
    );

    for (int cycle=0; cycle<2; cycle++)
    {
        for (int channel=0; channel<2; channel++)
        {
            N64CombinerType &m = mux.m_n64Combiners[cycle*2+channel];
            CombinerFormatType spl = mux.splitType[cycle*2+channel];
            bool isAlpha = (channel==1);

            if (spl == CM_FMT_TYPE_NOT_USED || spl == CM_FMT_TYPE_NOT_CHECKED)
                continue;

            const char * a = muxSrc(m.a, isAlpha);
            const char * b = muxSrc(m.b, isAlpha);
            const char * c = muxSrc(m.c, isAlpha);
            const char * d = muxSrc(m.d, isAlpha);

            char line[512] = "    comb.";
            strcat(line, isAlpha ? "a = " : "rgb = ");
            appendCombinerExpr(line, sizeof(line), spl, a, b, c, d);
            strcat(line, ";\n");
            strcat(hlsl, line);
        }
    }

    strcat(hlsl, "    return comb;\n}\n");

    DLOG("[GenerateShader] === HLSL generado ===");
    {
        const char *p = hlsl;
        int lineNum = 1;
        while (*p) {
            const char *eol = strchr(p, '\n');
            int len = eol ? (int)(eol - p) : (int)strlen(p);
            char lineBuf[512];
            if (len > 510) len = 510;
            memcpy(lineBuf, p, len);
            lineBuf[len] = 0;
            char hdr[600];
            sprintf(hdr, "  %3d: %s", lineNum, lineBuf);
            DLOG(hdr);
            lineNum++;
            if (!eol) break;
            p = eol + 1;
        }
    }
    DLOG("[GenerateShader] === fin HLSL ===");

    LPD3DXBUFFER pShaderCode = NULL, pErrors = NULL;
    HRESULT hr = D3DXCompileShader(hlsl, (UINT)strlen(hlsl), NULL, NULL,
        "main", "ps_3_0", 0, &pShaderCode, &pErrors, NULL);

    if (FAILED(hr))
    {
        char dbgerr[1024];
        if (pErrors)
        {
            sprintf(dbgerr, "[GenerateShader] D3DXCompileShader FALLO (hr=0x%08X): %s",
                (unsigned int)hr, (char*)pErrors->GetBufferPointer());
            DLOG(dbgerr);
            pErrors->Release();
        }
        else
        {
            sprintf(dbgerr, "[GenerateShader] D3DXCompileShader FALLO (hr=0x%08X) sin mensaje de error", (unsigned int)hr);
            DLOG(dbgerr);
        }
        return NULL;
    }

    char dbgok[256];
    sprintf(dbgok, "[GenerateShader] D3DXCompileShader OK, shaderCode=%p size=%u",
        pShaderCode->GetBufferPointer(), pShaderCode->GetBufferSize());
    DLOG(dbgok);

    IDirect3DPixelShader9 * shader = NULL;
    HRESULT hrCreate = g_pd3dDevice->CreatePixelShader((DWORD*)pShaderCode->GetBufferPointer(), &shader);
    sprintf(dbgok, "[GenerateShader] CreatePixelShader hr=0x%08X shader=%p",
        (unsigned int)hrCreate, (void*)shader);
    DLOG(dbgok);
    pShaderCode->Release();

    return shader;
}

int CxeColorCombiner::FindCompiledMux()
{
    for (uint32 i = 0; i < m_vCompiledShaders.size(); i++)
    {
        if (m_vCompiledShaders[i].dwMux0 == m_pDecodedMux->m_dwMux0
            && m_vCompiledShaders[i].dwMux1 == m_pDecodedMux->m_dwMux1
            && m_vCompiledShaders[i].fogIsUsed == (gRDP.bFogEnableInBlender && gRSP.bFogEnabled))
            return (int)i;
    }
    return -1;
}

void CxeColorCombiner::GenerateCombinerSetting(int index)
{
    static int callCount = 0;
    callCount++;

    if (index >= 0 && index < (int)m_vCompiledShaders.size())
    {
        IDirect3DPixelShader9 * ps = m_vCompiledShaders[index].shader;
        if (callCount <= 10 || (callCount % 200 == 0))
        {
            char dbg[256];
            sprintf(dbg, "[d3d9] GenerateCombinerSetting #%d: idx=%d shader=%p mux0=0x%08X mux1=0x%08X",
                callCount, index, (void*)ps, m_vCompiledShaders[index].dwMux0, m_vCompiledShaders[index].dwMux1);
            DLOG(dbg);
        }
        if (ps)
            g_pd3dDevice->SetPixelShader(ps);
        else
        {
            // shader was NULL (D3DXCompileShader failed or not loaded)
            if (callCount <= 3)
            {
                DLOG("[d3d9] GenerateCombinerSetting: shader is NULL! falling back to g_pPS_FB");
            }
            g_pd3dDevice->SetPixelShader(g_pPS_FB);
        }
    }
}

void CxeColorCombiner::GenerateCombinerSettingConstants(int index)
{
    float frac = gRDP.LODFrac / 255.0f;
    float frac2 = gRDP.primLODFrac / 255.0f;

    float consts[7][4] = {
        {0,0,0,0},
        {1,1,1,1},
        {gRDP.fvPrimitiveColor[0],gRDP.fvPrimitiveColor[1],gRDP.fvPrimitiveColor[2],gRDP.fvPrimitiveColor[3]},
        {gRDP.fvEnvColor[0],gRDP.fvEnvColor[1],gRDP.fvEnvColor[2],gRDP.fvEnvColor[3]},
        {frac,frac,frac,frac},
        {frac2,frac2,frac2,frac2},
        {-1,-1,-1,-1},
    };
    g_pd3dDevice->SetPixelShaderConstantF(0, (float*)consts, 7);
}
