#pragma once

// Reemplazo de xenos_backend.h para XDK / Xbox 360 (.xex)
// Mismo esqueleto de clases que el original (CxeGraphicsContext, CxeRender,
// CxeBlender, CxeTexture, CxeColorCombiner) para que el resto de
// Rice_GX_Xenos (Render.cpp, TextureManager.cpp, CombinerTable.cpp, etc.)
// no requiera cambios: solo cambia lo que hay DENTRO de estas clases.

#include <vector>
#include <xtl.h>   // XDK: reemplaza <xenos/xe.h> <debug.h> <time/time.h> <ppc/timebase.h>
#include <d3d9.h>  // En el XDK viene redefinido para Xbox 360 (mismo header xtl.h ya lo trae)

#define uint32 unsigned int
#include "GraphicsContext.h"
#include "Render.h"
#include "Combiner.h"
#include "blender.h"
#include "TextureManager.h"
#include "GeneralCombiner.h"
#include "../source/Rice_GX_Xenos/RenderBase.h"

// Sustitutos de macros que en el original venían de libxenon/SDL
#define SDL_WM_SetCaption(c,cc) OutputDebugStringA(c)
#define SDL_GetTicks() (GetTickCount())
#define usleep(x) Sleep((x)/1000)

class CxeGraphicsContext : public CGraphicsContext
{
    friend class CxeRender;
    friend class CxeRenderTexture;
public:
    virtual ~CxeGraphicsContext();

    bool Initialize(HWND hWnd, HWND hWndStatus, uint32 dwWidth, uint32 dwHeight, BOOL bWindowed );
    void CleanUp();
    void Clear(ClearFlag dwFlags, uint32 color=0xFF000000, float depth=1.0f);

    void UpdateFrame(bool swaponly=false);
    int ToggleFullscreen();     // no aplica en 360, se deja devolviendo 0

    bool IsExtensionSupported(const char* pExtName);
    bool IsWglExtensionSupported(const char* pExtName);
    static void InitDeviceParameters();

    bool IsSupportAnisotropicFiltering();
    int  getMaxAnisotropicFiltering();

protected:
    friend class CxeDeviceBuilder;
public:
    CxeGraphicsContext();
    void InitState(void);
    void InitOGLExtension(void);
    bool SetFullscreenMode();
    bool SetWindowMode();
};

class CxeRender : public CRender
{
    friend class CxeColorCombiner;
    friend class CxeBlender;
    friend class CxeDeviceBuilder;

protected:
    CxeRender();

public:
    ~CxeRender();
    void Initialize(void);

    bool InitDeviceObjects();
    bool ClearDeviceObjects();

    void SetShadeMode(RenderShadeMode mode);
    void ZBufferEnable(BOOL bZBuffer);
    void ClearBuffer(bool cbuffer, bool zbuffer);
    void ClearZBuffer(float depth);
    void SetZCompare(BOOL bZCompare);
    void SetZUpdate(BOOL bZUpdate);
    void SetZBias(int bias);
    void ApplyZBias(int bias);
    void SetAlphaRef(uint32 dwAlpha);
    void ForceAlphaRef(uint32 dwAlpha);
    void SetFillMode(FillMode mode);
    void SetViewportRender();
    void RenderReset();
    void SetCullMode(bool bCullFront, bool bCullBack);
    void SetAlphaTestEnable(BOOL bAlphaTestEnable);
    void UpdateScissor();
    void ApplyRDPScissor(bool force=false);
    void ApplyScissorWithClipRatio(bool force=false);

    bool SetCurrentTexture(int tile, CTexture *handler,uint32 dwTileWidth, uint32 dwTileHeight, TxtrCacheEntry *pTextureEntry);
    bool SetCurrentTexture(int tile, TxtrCacheEntry *pTextureEntry);
    void BindTexture(CxeTexture *texture, int unitno);
    void DisBindTexture(CxeTexture *texture, int unitno);
    void TexCoord2f(float u, float v);
    void TexCoord(TLITVERTEX &vtxInfo);
    void SetTextureUFlag(TextureUVFlag dwFlag, uint32 tile);
    void SetTextureVFlag(TextureUVFlag dwFlag, uint32 tile);
    void EnableTexUnit(int unitno, BOOL flag);
    void SetTexWrapS(int unitno,u32 flag);
    void SetTexWrapT(int unitno,u32 flag);
    void ApplyTextureFilter();
    void SetTextureToTextureUnitMap(int tex, int unit);

    void DrawSimple2DTexture(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, COLOR dif, COLOR spe, float z, float rhw);
    void DrawSimpleRect(int nX0, int nY0, int nX1, int nY1, uint32 dwColor, float depth, float rhw);
    void InitCombinerBlenderForSimpleRectDraw(uint32 tile=0);
    void DrawSpriteR_Render();
    void DrawObjBGCopy(uObjBg &info);
    void DrawText(const char* str, RECT *rect);

    void SetFogMinMax(float fMin, float fMax);
    void SetFogEnable(bool bEnable);
    void TurnFogOnOff(bool flag);
    void SetFogColor(uint32 r, uint32 g, uint32 b, uint32 a);

    void EndRendering(void);

    void glViewportWrapper(int x, int y, int width, int height, bool ortho);

protected:
    COLOR PostProcessDiffuseColor(COLOR curDiffuseColor);
    COLOR PostProcessSpecularColor();

    bool RenderFlushTris();
    bool RenderTexRect();
    bool RenderFillRect(uint32 dwColor, float depth);
    bool RenderLine3D();

    bool m_bSupportFogCoordExt;
    bool m_bSupportClampToEdge;
    bool m_bPolyOffset;

    int m_maxTexUnits;
    int m_textureUnitMap[8];
    BOOL    m_texUnitEnabled[8];
    CxeTexture*  m_curBoundTex[8];

private:
    void OneCLRVtx(u32 i,u32 j, float depth);
    void OneRTRVtx(u32 i);
    void OneRFRVtx(u32 i,u32 j, u32 dwColor, float depth);
    void OneDSTVtx(u32 i);
    void OneDSRRVtx(u32 i);
};

class CxeBlender : public CBlender
{
public:
    void NormalAlphaBlender(void);
    void DisableAlphaBlender(void);
    void BlendFunc(uint32 srcFunc, uint32 desFunc);
    void Enable();
    void Disable();

protected:
    friend class CxeDeviceBuilder;
    // D3DBLEND_ONE / D3DBLEND_ZERO en vez de XE_BLEND_ONE / XE_BLEND_ZERO
    CxeBlender(CRender *pRender) : CBlender(pRender),blend_src(D3DBLEND_ONE),blend_dst(D3DBLEND_ZERO) {};
    ~CxeBlender() {};

    int blend_src;
    int blend_dst;
};

class CxeTexture : public CTexture
{
    friend class CxeRenderTexture;
public:
    CxeTexture(uint32 dwWidth, uint32 dwHeight, TextureUsage usage = AS_NORMAL);
    ~CxeTexture();

    bool StartUpdate(DrawInfo *di);
    void EndUpdate(DrawInfo *di);

    // Devuelve el puntero real de D3D9, no m_pTexture (que nunca se llena
    // en este backend) - crucial para que SetCurrentTexture detecte
    // correctamente cuando la textura cambia.
    LPRICETEXTURE GetTexture() { return (LPRICETEXTURE)tex; }

    // Reemplaza "struct XenosSurface * tex;" por la textura nativa D3D9
    IDirect3DTexture9 * tex;
protected:
    friend class CxeDeviceBuilder;

    bool indirect;
};
class CxeRenderTexture : public CRenderTexture
{
public:
    CxeRenderTexture(int width, int height, RenderTextureInfo* pInfo, TextureUsage usage);
    ~CxeRenderTexture();
    bool SetAsRenderTarget(bool enable);
    void LoadTexture(TxtrCacheEntry* pEntry);
    void StoreToRDRAM(int infoIdx);
    IDirect3DSurface9 * GetRTSurface() { return m_pRTSurface; }
    IDirect3DTexture9 * GetD3DTexture() { return m_pD3DTexture; }
protected:
    IDirect3DTexture9 *      m_pD3DTexture;   // textura destino (D3DUSAGE_RENDERTARGET), tileada
    IDirect3DSurface9 *      m_pRTSurface;    // superficie de la textura, para SetRenderTarget
    IDirect3DSurface9 *      m_pOldRTSurface; // back buffer original, para restaurar
    IDirect3DSurface9 *      m_pOldDSSurface; // depth-stencil original, para restaurar
    IDirect3DTexture9 *      m_pResolvedTexture; // copia lineal, resultado del Resolve; esto es lo que se muestrea
    IDirect3DSurface9 *      m_pResolvedSurface; // superficie de m_pResolvedTexture (destino del Resolve)
};

typedef struct {
    uint32  dwMux0;
    uint32  dwMux1;

    bool    fogIsUsed;
    // Reemplaza "struct XenosShader * shader;" por el pixel shader D3D9
    IDirect3DPixelShader9 * shader;
} xeShaderCombinerSaveType;

class CxeColorCombiner : public CColorCombiner
{
public:
    bool Initialize(void);
    void InitCombinerBlenderForSimpleTextureDraw(uint32 tile=0);
protected:
    friend class CxeDeviceBuilder;

    void DisableCombiner(void);
    void InitCombinerCycleCopy(void);
    void InitCombinerCycleFill(void);
    void InitCombinerCycle12(void);

    CxeColorCombiner(CRender *pRender);
    ~CxeColorCombiner();

    int m_lastIndex;
    uint32 m_dwLastMux0;
    uint32 m_dwLastMux1;
    std::vector<xeShaderCombinerSaveType> m_vCompiledShaders;

    CxeRender *m_pxeRender;

private:
    virtual int ParseDecodedMux();
    IDirect3DPixelShader9 * GenerateShader();
    int FindCompiledMux();
    virtual void GenerateCombinerSetting(int index);
    virtual void GenerateCombinerSettingConstants(int index);
};

// Handle global al device D3D9 de Xbox 360 (equivalente al "xe" global del original)
extern IDirect3DDevice9 * g_pd3dDevice;
