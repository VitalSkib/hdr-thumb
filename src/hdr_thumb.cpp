/**
 * HDR + EXR + TGA + SVG Thumbnail / Preview / Extract Provider for Windows Explorer
 *
 * Implements:
 *   IThumbnailProvider  -- icon grid thumbnails (Explorer, file managers)
 *   IPreviewHandler     -- Details pane preview (right panel in Explorer)
 *   IExtractImage       -- legacy thumbnail extraction (Total Commander etc.)
 *
 * Formats:
 *   .hdr  Radiance RGBE   via stb_image  (Reinhard tonemapping)
 *   .exr  OpenEXR         via tinyexr    (Reinhard tonemapping, scanline+tiled)
 *   .tga  Targa           via stb_image  (LDR, direct)
 *   .svg  SVG 1.1         via lunasvg    (full CSS, currentColor, transparent)
 *
 * Build:    build.bat    (requires lib\liblunasvg.a)
 * Register: regsvr32 hdr_thumb.dll (as Administrator)
 */

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <thumbcache.h>
#include <new>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <algorithm>

#ifndef WTSAT_OPAQUE
#define WTSAT_OPAQUE ((WTS_ALPHATYPE)3)
#endif
#ifndef WTSAT_ARGB
#define WTSAT_ARGB ((WTS_ALPHATYPE)2)
#endif

// ── stb_image: HDR and TGA decoding ──────────────────────────────────────────
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_JPEG
#define STBI_NO_PNG
#define STBI_NO_BMP
#define STBI_NO_GIF
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../include/stb_image.h"
#pragma GCC diagnostic pop

// ── tinyexr: OpenEXR decoding ─────────────────────────────────────────────────
#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 1
#include "../include/tinyexr.h"

// ── lunasvg: full SVG 1.1 rendering ──────────────────────────────────────────
#include "../include/lunasvg.h"

// ── IPreviewHandler (define manually for MinGW compatibility) ─────────────────
#ifndef __IPreviewHandler_INTERFACE_DEFINED__
#define __IPreviewHandler_INTERFACE_DEFINED__
struct IPreviewHandler : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetWindow(HWND hwnd, const RECT *prc) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetRect(const RECT *prc) = 0;
    virtual HRESULT STDMETHODCALLTYPE DoPreview() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unload() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetFocus() = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryFocus(HWND *phwnd) = 0;
    virtual HRESULT STDMETHODCALLTYPE TranslateAccelerator(MSG *pmsg) = 0;
};
#endif

// ── CLSIDs ────────────────────────────────────────────────────────────────────
static const CLSID CLSID_HdrThumbProvider = {
    0x6A7B3E40,0x1234,0x4321,{0xAB,0xCD,0x9F,0x0E,0x1D,0x2C,0x3B,0x4A}
};
static const wchar_t* CLSID_HDR_STR = L"{6A7B3E40-1234-4321-ABCD-9F0E1D2C3B4A}";

static const CLSID CLSID_ExrThumbProvider = {
    0x7B8C4F51,0x2345,0x5432,{0xBC,0xDE,0xA0,0x1F,0x2E,0x3D,0x4C,0x5B}
};
static const wchar_t* CLSID_EXR_STR = L"{7B8C4F51-2345-5432-BCDE-A01F2E3D4C5B}";

static const CLSID CLSID_TgaThumbProvider = {
    0x8C9D5F62,0x3456,0x6543,{0xCD,0xEF,0xB1,0x20,0x3F,0x4E,0x5D,0x6C}
};
static const wchar_t* CLSID_TGA_STR = L"{8C9D5F62-3456-6543-CDEF-B1203F4E5D6C}";

static const CLSID CLSID_SvgThumbProvider = {
    0x9DAE6073,0x4567,0x7654,{0xDE,0xF0,0xC2,0x31,0x40,0x5F,0x6E,0x7D}
};
static const wchar_t* CLSID_SVG_STR = L"{9DAE6073-4567-7654-DEF0-C231405F6E7D}";

// Shell extension GUIDs
static const wchar_t* SHELL_THUMB_GUID    = L"{e357fccd-a995-4576-b01f-234630154e96}";
static const wchar_t* SHELL_PREVIEW_GUID  = L"{8895b1c6-b41f-4c1c-a562-0d564250836f}";
static const wchar_t* SHELL_EXTRACT_GUID  = L"{BB2E617C-0920-11d1-9A0B-00C04FC2D6C1}";

// IIDs
static const IID IID_IThumbnailProvider_ = {
    0xe357fccd,0xa995,0x4576,{0xb0,0x1f,0x23,0x46,0x30,0x15,0x4e,0x96}
};
static const IID IID_IInitializeWithStream_ = {
    0xb824b49d,0x22ac,0x4161,{0xac,0x8a,0x99,0x16,0xe8,0xfa,0x3f,0x7f}
};
static const IID IID_IPreviewHandler_ = {
    0x8895b1c6,0xb41f,0x4c1c,{0xa5,0x62,0x0d,0x56,0x42,0x50,0x83,0x6f}
};
static const IID IID_IOleWindow_ = {
    0x00000114,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}
};
static const IID IID_IExtractImage_ = {
    0xbb2e617c,0x0920,0x11d1,{0x9a,0x0b,0x00,0xc0,0x4f,0xc2,0xd6,0xc1}
};
static const IID IID_IPersistFile_ = {
    0x0000010b,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}
};
static const IID IID_IPersist_ = {
    0x0000010c,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}
};

// ── Utility functions ─────────────────────────────────────────────────────────

static HMODULE g_hModule = NULL;

static inline float reinhard(float v) { return v / (1.f + v); }
static inline BYTE toU8(float v) {
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    return (BYTE)(v * 255.f + 0.5f);
}
static inline float srgb(float v) { return powf(v < 0.f ? 0.f : v, 1.f / 2.2f); }

static void thumbSize(int w, int h, UINT cx, int& dw, int& dh) {
    if (w >= h) { dw = (int)cx; dh = std::max(1, (int)((float)cx * h / w)); }
    else        { dh = (int)cx; dw = std::max(1, (int)((float)cx * w / h)); }
}

static void bilerp(const float* src, int sw, int sh, float* dst, int dw, int dh) {
    float sx = (float)sw / dw, sy = (float)sh / dh;
    for (int dy = 0; dy < dh; dy++) {
        float fy = (dy + .5f) * sy - .5f;
        int y0 = (int)fy; if (y0 < 0) y0 = 0;
        int y1 = y0 + 1;  if (y1 >= sh) y1 = sh - 1;
        float wy = fy - y0;
        for (int dx = 0; dx < dw; dx++) {
            float fx = (dx + .5f) * sx - .5f;
            int x0 = (int)fx; if (x0 < 0) x0 = 0;
            int x1 = x0 + 1;  if (x1 >= sw) x1 = sw - 1;
            float wx = fx - x0;
            for (int c = 0; c < 3; c++)
                dst[(dy * dw + dx) * 3 + c] =
                    src[(y0 * sw + x0) * 3 + c] * (1 - wx) * (1 - wy) +
                    src[(y0 * sw + x1) * 3 + c] *      wx  * (1 - wy) +
                    src[(y1 * sw + x0) * 3 + c] * (1 - wx) *      wy  +
                    src[(y1 * sw + x1) * 3 + c] *      wx  *      wy;
        }
    }
}

static HBITMAP FloatRGBtoHBitmap(const float* rgb, int w, int h) {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w; bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC hdc = GetDC(NULL);
    HBITMAP hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, hdc);
    if (!hbm || !bits) return NULL;
    BYTE* pix = (BYTE*)bits;
    for (int i = 0; i < w * h; i++) {
        pix[i*4+0] = toU8(srgb(reinhard(rgb[i*3+2])));
        pix[i*4+1] = toU8(srgb(reinhard(rgb[i*3+1])));
        pix[i*4+2] = toU8(srgb(reinhard(rgb[i*3+0])));
        pix[i*4+3] = 0xFF;
    }
    return hbm;
}

static HBITMAP ByteRGBAtoHBitmap(const BYTE* rgba, int w, int h) {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w; bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC hdc = GetDC(NULL);
    HBITMAP hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, hdc);
    if (!hbm || !bits) return NULL;
    BYTE* pix = (BYTE*)bits;
    for (int i = 0; i < w * h; i++) {
        pix[i*4+0] = rgba[i*4+2];
        pix[i*4+1] = rgba[i*4+1];
        pix[i*4+2] = rgba[i*4+0];
        pix[i*4+3] = rgba[i*4+3];
    }
    return hbm;
}

// ── HDR decoder ───────────────────────────────────────────────────────────────
static HBITMAP DecodeHDR(const BYTE* data, DWORD size, UINT cx) {
    int w, h, comp;
    float* hdr = stbi_loadf_from_memory(data, (int)size, &w, &h, &comp, 3);
    if (!hdr) return NULL;
    int dw, dh; thumbSize(w, h, cx, dw, dh);
    float* sc = (float*)malloc(dw * dh * 3 * sizeof(float));
    if (!sc) { stbi_image_free(hdr); return NULL; }
    bilerp(hdr, w, h, sc, dw, dh);
    stbi_image_free(hdr);
    HBITMAP hbm = FloatRGBtoHBitmap(sc, dw, dh);
    free(sc); return hbm;
}

// ── EXR decoder (tinyexr v0.9.5, scanline + tiled) ───────────────────────────
static HBITMAP DecodeEXR(const BYTE* data, DWORD, UINT cx) {
    EXRVersion version;
    if (ParseEXRVersionFromMemory(&version, data) != TINYEXR_SUCCESS) return NULL;
    EXRHeader header; InitEXRHeader(&header);
    const char* err = NULL;
    if (ParseEXRHeaderFromMemory(&header, &version, data, &err) != TINYEXR_SUCCESS) return NULL;
    for (int i = 0; i < header.num_channels; i++)
        header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;
    EXRImage image; InitEXRImage(&image);
    if (LoadEXRImageFromMemory(&image, &header, data, &err) != TINYEXR_SUCCESS) {
        FreeEXRHeader(&header); return NULL;
    }
    int w = image.width, h = image.height;
    int idxR = -1, idxG = -1, idxB = -1;
    for (int i = 0; i < header.num_channels; i++) {
        const char* n = header.channels[i].name;
        if      (strcmp(n, "R") == 0) idxR = i;
        else if (strcmp(n, "G") == 0) idxG = i;
        else if (strcmp(n, "B") == 0) idxB = i;
    }
    if (idxR < 0) idxR = 0;
    if (idxG < 0) idxG = (header.num_channels > 1) ? 1 : 0;
    if (idxB < 0) idxB = (header.num_channels > 2) ? 2 : 0;
    float* rgb = (float*)calloc(w * h * 3, sizeof(float));
    if (!rgb) { FreeEXRImage(&image); FreeEXRHeader(&header); return NULL; }
    if (header.tiled && image.tiles) {
        for (int t = 0; t < image.num_tiles; t++) {
            const EXRTile& tile = image.tiles[t];
            int tx = tile.offset_x * header.tile_size_x;
            int ty = tile.offset_y * header.tile_size_y;
            float** tp = reinterpret_cast<float**>(tile.images);
            for (int py = 0; py < tile.height; py++) {
                int gy = ty + py; if (gy >= h) break;
                for (int px = 0; px < tile.width; px++) {
                    int gx = tx + px; if (gx >= w) break;
                    int dst = (gy * w + gx) * 3, src = py * tile.width + px;
                    rgb[dst+0] = tp[idxR][src];
                    rgb[dst+1] = tp[idxG][src];
                    rgb[dst+2] = tp[idxB][src];
                }
            }
        }
    } else {
        float** imgs = reinterpret_cast<float**>(image.images);
        for (int i = 0; i < w * h; i++) {
            rgb[i*3+0] = imgs[idxR][i];
            rgb[i*3+1] = imgs[idxG][i];
            rgb[i*3+2] = imgs[idxB][i];
        }
    }
    FreeEXRImage(&image); FreeEXRHeader(&header);
    int dw, dh; thumbSize(w, h, cx, dw, dh);
    float* sc = (float*)malloc(dw * dh * 3 * sizeof(float));
    if (!sc) { free(rgb); return NULL; }
    bilerp(rgb, w, h, sc, dw, dh); free(rgb);
    HBITMAP hbm = FloatRGBtoHBitmap(sc, dw, dh);
    free(sc); return hbm;
}

// ── TGA decoder ───────────────────────────────────────────────────────────────
static HBITMAP DecodeTGA(const BYTE* data, DWORD size, UINT cx) {
    int w, h, comp;
    BYTE* pix = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 4);
    if (!pix) return NULL;
    int dw, dh; thumbSize(w, h, cx, dw, dh);
    BYTE* sc = (BYTE*)malloc(dw * dh * 4);
    if (!sc) { stbi_image_free(pix); return NULL; }
    for (int dy = 0; dy < dh; dy++) {
        int sy = dy * h / dh;
        for (int dx = 0; dx < dw; dx++) {
            int sx = dx * w / dw;
            memcpy(&sc[(dy*dw+dx)*4], &pix[(sy*w+sx)*4], 4);
        }
    }
    stbi_image_free(pix);
    HBITMAP hbm = ByteRGBAtoHBitmap(sc, dw, dh);
    free(sc); return hbm;
}

// ── SVG decoder via lunasvg ───────────────────────────────────────────────────
static HBITMAP DecodeSVG(const BYTE* data, DWORD size, UINT cx) {
    char* buf = (char*)malloc(size + 1);
    if (!buf) return NULL;
    memcpy(buf, data, size); buf[size] = '\0';
    auto doc = lunasvg::Document::loadFromData(buf, (size_t)size);
    free(buf);
    if (!doc) return NULL;
    double sw = doc->width(), sh = doc->height();
    if (sw < 1.0 || sh < 1.0) { sw = 512.0; sh = 512.0; }
    int dw, dh; thumbSize((int)sw, (int)sh, cx, dw, dh);
    auto bmp = doc->renderToBitmap((uint32_t)dw, (uint32_t)dh, 0x00000000);
    if (bmp.isNull()) return NULL;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = dw; bmi.bmiHeader.biHeight = -dh;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC hdc = GetDC(NULL);
    HBITMAP hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, hdc);
    if (!hbm || !bits) return NULL;
    memcpy(bits, bmp.data(), (size_t)dw * dh * 4);
    return hbm;
}

// ── SVG decoder for preview (renders at exact size) ───────────────────────────
static HBITMAP DecodeSVGPreview(const BYTE* data, DWORD size, int pw, int ph) {
    char* buf = (char*)malloc(size + 1);
    if (!buf) return NULL;
    memcpy(buf, data, size); buf[size] = '\0';
    auto doc = lunasvg::Document::loadFromData(buf, (size_t)size);
    free(buf);
    if (!doc) return NULL;
    double sw = doc->width(), sh = doc->height();
    if (sw < 1.0 || sh < 1.0) { sw = 512.0; sh = 512.0; }
    // Fit into preview rect maintaining aspect ratio
    float scale = std::min((float)pw / sw, (float)ph / sh);
    int dw = std::max(1, (int)(sw * scale));
    int dh = std::max(1, (int)(sh * scale));
    auto bmp = doc->renderToBitmap((uint32_t)dw, (uint32_t)dh, 0x00000000);
    if (bmp.isNull()) return NULL;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = dw; bmi.bmiHeader.biHeight = -dh;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC hdc = GetDC(NULL);
    HBITMAP hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, hdc);
    if (!hbm || !bits) return NULL;
    // Composite over white background for SVG preview
    const BYTE* src = bmp.data();
    BYTE* dst = (BYTE*)bits;
    for (int i = 0; i < dw * dh; i++) {
        int b = src[i*4+0], g = src[i*4+1], r = src[i*4+2], a = src[i*4+3];
        int ia = 255 - a;
        dst[i*4+0] = (BYTE)std::min(255, b + ia);
        dst[i*4+1] = (BYTE)std::min(255, g + ia);
        dst[i*4+2] = (BYTE)std::min(255, r + ia);
        dst[i*4+3] = 0xFF;
    }
    return hbm;
}

// ── Preview window WndProc ────────────────────────────────────────────────────
static const wchar_t* PREVIEW_WND_CLASS = L"HdrThumbPreviewWnd";

struct PreviewState {
    HBITMAP hbm;
    bool    hasAlpha;  // true for SVG (premultiplied ARGB)
};

static LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PreviewState* ps = (PreviewState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    // Let parent background show through — no fill
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT && ps) {
        PAINTSTRUCT paint;
        HDC hdc = BeginPaint(hwnd, &paint);
        RECT rc; GetClientRect(hwnd, &rc);
        int cw = rc.right, ch = rc.bottom;
        if (ps->hbm && cw > 0 && ch > 0) {
            BITMAP bm; GetObject(ps->hbm, sizeof(bm), &bm);
            float scale = std::min((float)cw / bm.bmWidth, (float)ch / bm.bmHeight);
            int dw = (int)(bm.bmWidth  * scale);
            int dh = (int)(bm.bmHeight * scale);
            int dx = (cw - dw) / 2;
            int dy = (ch - dh) / 2;
            HDC hdcMem = CreateCompatibleDC(hdc);
            HGDIOBJ old = SelectObject(hdcMem, ps->hbm);
            if (ps->hasAlpha) {
                // SVG: use AlphaBlend for proper transparency
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                AlphaBlend(hdc, dx, dy, dw, dh, hdcMem, 0, 0, bm.bmWidth, bm.bmHeight, bf);
            } else {
                SetStretchBltMode(hdc, HALFTONE);
                SetBrushOrgEx(hdc, 0, 0, NULL);
                StretchBlt(hdc, dx, dy, dw, dh, hdcMem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
            }
            SelectObject(hdcMem, old);
            DeleteDC(hdcMem);
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (msg == WM_DESTROY) return 0;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RegisterPreviewClass() {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = PreviewWndProc;
    wc.hInstance     = g_hModule;
    wc.lpszClassName = PREVIEW_WND_CLASS;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc); // ignore error if already registered
}

// ── Read stream helper ────────────────────────────────────────────────────────
static HRESULT ReadStream(IStream* pStream, BYTE** ppBuf, DWORD* pSize,
                          ULONGLONG maxBytes = 512ULL * 1024 * 1024) {
    STATSTG st = {};
    HRESULT hr = pStream->Stat(&st, STATFLAG_NONAME);
    if (FAILED(hr)) return hr;
    ULONGLONG sz64 = st.cbSize.QuadPart;
    if (!sz64) return E_FAIL;
    if (maxBytes && sz64 > maxBytes) return E_FAIL;
    DWORD sz = (DWORD)sz64;
    BYTE* buf = (BYTE*)malloc(sz);
    if (!buf) return E_OUTOFMEMORY;
    LARGE_INTEGER li = {}; pStream->Seek(li, STREAM_SEEK_SET, NULL);
    ULONG read = 0;
    hr = pStream->Read(buf, sz, &read);
    if (FAILED(hr) || read != sz) { free(buf); return E_FAIL; }
    *ppBuf = buf; *pSize = sz;
    return S_OK;
}

// ── Base Provider ─────────────────────────────────────────────────────────────
// Implements IThumbnailProvider + IInitializeWithStream + IPreviewHandler +
//            IOleWindow + IExtractImage + IPersistFile
//
class BaseProvider : public IInitializeWithStream,
                     public IThumbnailProvider,
                     public IPreviewHandler,
                     public IOleWindow,
                     public IExtractImage,
                     public IPersistFile
{
public:
    BaseProvider() : m_cRef(1), m_pStream(NULL),
                     m_hwndParent(NULL), m_hwndPreview(NULL),
                     m_previewState{NULL, false}
    {
        m_rect = {};
        m_extractSize = {};
    }
    virtual ~BaseProvider() {
        UnloadPreview();
        if (m_pStream) { m_pStream->Release(); m_pStream = NULL; }
    }

    // ── IUnknown ──
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&m_cRef);
        if (r == 0) delete this;
        return r;
    }
    // QI implemented in derived classes via IMPL_QI macro

    // ── IInitializeWithStream ──
    STDMETHODIMP Initialize(IStream* ps, DWORD) override {
        if (m_pStream) m_pStream->Release();
        m_pStream = ps; m_pStream->AddRef();
        return S_OK;
    }

    // ── IThumbnailProvider ──
    STDMETHODIMP GetThumbnail(UINT cx, HBITMAP* ph, WTS_ALPHATYPE* pa) override {
        if (!ph || !pa) return E_POINTER;
        if (!m_pStream) return E_UNEXPECTED;
        *ph = NULL; *pa = AlphaType();
        BYTE* buf = NULL; DWORD size = 0;
        HRESULT hr = ReadStream(m_pStream, &buf, &size, MaxFileBytes());
        if (FAILED(hr)) return hr;
        HBITMAP hbm = Decode(buf, size, cx);
        free(buf);
        if (!hbm) return E_FAIL;
        *ph = hbm; return S_OK;
    }

    // ── IPreviewHandler ──
    STDMETHODIMP SetWindow(HWND hwnd, const RECT* prc) override {
        if (!hwnd || !prc) return E_INVALIDARG;
        m_hwndParent = hwnd; m_rect = *prc;
        if (m_hwndPreview)
            SetWindowPos(m_hwndPreview, NULL,
                m_rect.left, m_rect.top,
                m_rect.right - m_rect.left, m_rect.bottom - m_rect.top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        return S_OK;
    }
    STDMETHODIMP SetRect(const RECT* prc) override {
        if (!prc) return E_INVALIDARG;
        m_rect = *prc;
        if (m_hwndPreview)
            SetWindowPos(m_hwndPreview, NULL,
                m_rect.left, m_rect.top,
                m_rect.right - m_rect.left, m_rect.bottom - m_rect.top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        return S_OK;
    }
    STDMETHODIMP DoPreview() override {
        if (!m_pStream || !m_hwndParent) return E_UNEXPECTED;
        UnloadPreview();
        BYTE* buf = NULL; DWORD size = 0;
        HRESULT hr = ReadStream(m_pStream, &buf, &size, MaxFileBytes());
        if (FAILED(hr)) return hr;
        int pw = m_rect.right - m_rect.left;
        int ph = m_rect.bottom - m_rect.top;
        if (pw < 1) pw = 256;
        if (ph < 1) ph = 256;
        m_previewState.hasAlpha = IsSvg();
        m_previewState.hbm = DecodeForPreview(buf, size, pw, ph);
        free(buf);
        if (!m_previewState.hbm) return E_FAIL;
        RegisterPreviewClass();
        m_hwndPreview = CreateWindowExW(0, PREVIEW_WND_CLASS, NULL,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            m_rect.left, m_rect.top, pw, ph,
            m_hwndParent, NULL, g_hModule, &m_previewState);
        return m_hwndPreview ? S_OK : E_FAIL;
    }
    STDMETHODIMP Unload() override { UnloadPreview(); return S_OK; }
    STDMETHODIMP SetFocus() override {
        if (m_hwndPreview) ::SetFocus(m_hwndPreview);
        return S_OK;
    }
    STDMETHODIMP QueryFocus(HWND* phwnd) override {
        if (!phwnd) return E_POINTER;
        *phwnd = ::GetFocus(); return S_OK;
    }
    STDMETHODIMP TranslateAccelerator(MSG*) override { return E_NOTIMPL; }

    // ── IOleWindow ──
    STDMETHODIMP GetWindow(HWND* phwnd) override {
        if (!phwnd) return E_POINTER;
        *phwnd = m_hwndParent; return S_OK;
    }
    STDMETHODIMP ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }

    // ── IExtractImage ──
    STDMETHODIMP GetLocation(LPWSTR pszPathBuffer, DWORD cch,
                             DWORD* pdwPriority, const SIZE* prgSize,
                             DWORD, DWORD* pdwFlags) override {
        if (prgSize) m_extractSize = *prgSize;
        if (pdwPriority) *pdwPriority = 0;
        if (pdwFlags) *pdwFlags = 0;
        if (pszPathBuffer && cch > 0) pszPathBuffer[0] = L'\0';
        return S_OK;
    }
    STDMETHODIMP Extract(HBITMAP* phBmpImage) override {
        if (!phBmpImage) return E_POINTER;
        *phBmpImage = NULL;
        if (!m_pStream) return E_UNEXPECTED;
        BYTE* buf = NULL; DWORD size = 0;
        HRESULT hr = ReadStream(m_pStream, &buf, &size, MaxFileBytes());
        if (FAILED(hr)) return hr;
        UINT cx = (UINT)std::max(m_extractSize.cx, m_extractSize.cy);
        if (cx < 1) cx = 256;
        HBITMAP hbm = Decode(buf, size, cx);
        free(buf);
        if (!hbm) return E_FAIL;
        *phBmpImage = hbm; return S_OK;
    }

    // ── IPersistFile ──
    STDMETHODIMP GetClassID(CLSID*) override { return E_NOTIMPL; }
    STDMETHODIMP IsDirty() override { return S_FALSE; }
    STDMETHODIMP Load(LPCOLESTR pszFileName, DWORD) override {
        IStream* pStream = NULL;
        HRESULT hr = SHCreateStreamOnFileW(pszFileName, STGM_READ | STGM_SHARE_DENY_WRITE, &pStream);
        if (FAILED(hr)) return hr;
        if (m_pStream) m_pStream->Release();
        m_pStream = pStream;
        return S_OK;
    }
    STDMETHODIMP Save(LPCOLESTR, BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP SaveCompleted(LPCOLESTR) override { return E_NOTIMPL; }
    STDMETHODIMP GetCurFile(LPOLESTR*) override { return E_NOTIMPL; }

protected:
    virtual ULONGLONG     MaxFileBytes() { return 512ULL * 1024 * 1024; }
    virtual WTS_ALPHATYPE AlphaType()   { return WTSAT_OPAQUE; }
    virtual bool          IsSvg()       { return false; }

    // Decode for thumbnail / extract (single max dimension cx)
    virtual HBITMAP Decode(const BYTE* d, DWORD s, UINT cx) = 0;

    // Decode for preview (full width x height available)
    // Default: use max dimension; SVG overrides to use exact size
    virtual HBITMAP DecodeForPreview(const BYTE* d, DWORD s, int pw, int ph) {
        return Decode(d, s, (UINT)std::min(std::max(pw, ph) * 2, 2048));
    }

    void UnloadPreview() {
        if (m_hwndPreview) { DestroyWindow(m_hwndPreview); m_hwndPreview = NULL; }
        if (m_previewState.hbm) { DeleteObject(m_previewState.hbm); m_previewState.hbm = NULL; }
    }

    volatile LONG m_cRef;
    IStream*      m_pStream;
    HWND          m_hwndParent;
    HWND          m_hwndPreview;
    PreviewState  m_previewState;
    RECT          m_rect;
    SIZE          m_extractSize;
};

// ── Provider subclasses ───────────────────────────────────────────────────────

#define IMPL_QI \
    STDMETHODIMP QueryInterface(REFIID r, void** p) override { \
        if (!p) return E_POINTER; \
        if (IsEqualIID(r, IID_IUnknown) || IsEqualIID(r, IID_IInitializeWithStream_)) \
            *p = static_cast<IInitializeWithStream*>(this); \
        else if (IsEqualIID(r, IID_IThumbnailProvider_)) \
            *p = static_cast<IThumbnailProvider*>(this); \
        else if (IsEqualIID(r, IID_IPreviewHandler_)) \
            *p = static_cast<IPreviewHandler*>(this); \
        else if (IsEqualIID(r, IID_IOleWindow_)) \
            *p = static_cast<IOleWindow*>(this); \
        else if (IsEqualIID(r, IID_IExtractImage_)) \
            *p = static_cast<IExtractImage*>(this); \
        else if (IsEqualIID(r, IID_IPersistFile_) || IsEqualIID(r, IID_IPersist_)) \
            *p = static_cast<IPersistFile*>(this); \
        else { *p = NULL; return E_NOINTERFACE; } \
        AddRef(); return S_OK; \
    }

class HdrThumbProvider : public BaseProvider {
public:
    IMPL_QI
protected:
    HBITMAP Decode(const BYTE* d, DWORD s, UINT cx) override { return DecodeHDR(d, s, cx); }
};

class ExrThumbProvider : public BaseProvider {
public:
    IMPL_QI
protected:
    HBITMAP Decode(const BYTE* d, DWORD s, UINT cx) override { return DecodeEXR(d, s, cx); }
};

class TgaThumbProvider : public BaseProvider {
public:
    IMPL_QI
protected:
    HBITMAP Decode(const BYTE* d, DWORD s, UINT cx) override { return DecodeTGA(d, s, cx); }
};

class SvgThumbProvider : public BaseProvider {
public:
    IMPL_QI
protected:
    WTS_ALPHATYPE AlphaType() override { return WTSAT_ARGB; }
    bool          IsSvg()     override { return true; }
    HBITMAP Decode(const BYTE* d, DWORD s, UINT cx) override { return DecodeSVG(d, s, cx); }
    HBITMAP DecodeForPreview(const BYTE* d, DWORD s, int pw, int ph) override {
        return DecodeSVGPreview(d, s, pw, ph);
    }
};

#undef IMPL_QI

// ── Class factory ─────────────────────────────────────────────────────────────
template<class T>
class ClassFactory : public IClassFactory {
    volatile LONG m_cRef;
public:
    ClassFactory() : m_cRef(1) {}
    virtual ~ClassFactory() {}
    STDMETHODIMP QueryInterface(REFIID r, void** p) override {
        if (!p) return E_POINTER;
        if (IsEqualIID(r, IID_IUnknown) || IsEqualIID(r, IID_IClassFactory))
            { *p = static_cast<IClassFactory*>(this); AddRef(); return S_OK; }
        *p = NULL; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()  override { return InterlockedIncrement(&m_cRef); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&m_cRef);
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP CreateInstance(IUnknown* o, REFIID r, void** p) override {
        if (o) return CLASS_E_NOAGGREGATION;
        T* t = new(std::nothrow) T();
        if (!t) return E_OUTOFMEMORY;
        HRESULT hr = t->QueryInterface(r, p); t->Release(); return hr;
    }
    STDMETHODIMP LockServer(BOOL) override { return S_OK; }
};

// ── DLL entry points ──────────────────────────────────────────────────────────
extern "C" BOOL WINAPI DllMain(HMODULE hm, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) { g_hModule = hm; DisableThreadLibraryCalls(hm); }
    return TRUE;
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DllGetClassObject(REFCLSID rc, REFIID ri, void** pv) {
    if (!pv) return E_POINTER;
#define TRY_CLSID(CLS, PROV) \
    if (IsEqualCLSID(rc, CLS)) { \
        auto* p = new(std::nothrow) ClassFactory<PROV>(); \
        if (!p) return E_OUTOFMEMORY; \
        HRESULT hr = p->QueryInterface(ri, pv); p->Release(); return hr; \
    }
    TRY_CLSID(CLSID_HdrThumbProvider, HdrThumbProvider)
    TRY_CLSID(CLSID_ExrThumbProvider, ExrThumbProvider)
    TRY_CLSID(CLSID_TgaThumbProvider, TgaThumbProvider)
    TRY_CLSID(CLSID_SvgThumbProvider, SvgThumbProvider)
#undef TRY_CLSID
    return CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DllCanUnloadNow(void) { return S_OK; }

// ── Registry helpers ──────────────────────────────────────────────────────────
static void RegSetSZ(HKEY root, const wchar_t* path, const wchar_t* val, const wchar_t* data) {
    HKEY hk;
    RegCreateKeyExW(root, path, 0, NULL, 0, KEY_SET_VALUE | KEY_CREATE_SUB_KEY, NULL, &hk, NULL);
    RegSetValueExW(hk, val, 0, REG_SZ, (const BYTE*)data, (DWORD)(wcslen(data)+1)*2);
    RegCloseKey(hk);
}
static void RegSetDWORD(HKEY root, const wchar_t* path, const wchar_t* val, DWORD data) {
    HKEY hk;
    RegCreateKeyExW(root, path, 0, NULL, 0, KEY_SET_VALUE | KEY_CREATE_SUB_KEY, NULL, &hk, NULL);
    RegSetValueExW(hk, val, 0, REG_DWORD, (const BYTE*)&data, sizeof(DWORD));
    RegCloseKey(hk);
}

// ── DllRegisterServer / DllUnregisterServer ───────────────────────────────────
extern "C" __declspec(dllexport)
HRESULT WINAPI DllRegisterServer(void) {
    wchar_t dll[MAX_PATH];
    GetModuleFileNameW(g_hModule, dll, MAX_PATH);

    struct { const wchar_t* clsid; const wchar_t* label; } cls[] = {
        { CLSID_HDR_STR, L"HDR Thumbnail Provider" },
        { CLSID_EXR_STR, L"EXR Thumbnail Provider" },
        { CLSID_TGA_STR, L"TGA Thumbnail Provider" },
        { CLSID_SVG_STR, L"SVG Thumbnail Provider" },
    };
    for (auto& e : cls) {
        wchar_t k1[256], k2[256];
        wsprintfW(k1, L"CLSID\\%s",                 e.clsid);
        wsprintfW(k2, L"CLSID\\%s\\InprocServer32", e.clsid);
        RegSetSZ   (HKEY_CLASSES_ROOT, k1, NULL,                      e.label);
        RegSetSZ   (HKEY_CLASSES_ROOT, k2, NULL,                      dll);
        RegSetSZ   (HKEY_CLASSES_ROOT, k2, L"ThreadingModel",         L"Apartment");
        RegSetDWORD(HKEY_CLASSES_ROOT, k1, L"DisableProcessIsolation", 1);
    }

    // Register all three shell extension types per extension
    struct { const wchar_t* ext; const wchar_t* clsid; } exts[] = {
        { L".hdr", CLSID_HDR_STR },
        { L".exr", CLSID_EXR_STR },
        { L".tga", CLSID_TGA_STR },
        { L".svg", CLSID_SVG_STR },
    };
    for (auto& e : exts) {
        wchar_t k[256];
        // Thumbnail
        wsprintfW(k, L"%s\\ShellEx\\%s", e.ext, SHELL_THUMB_GUID);
        RegSetSZ(HKEY_CLASSES_ROOT, k, NULL, e.clsid);
        // Preview handler (Details pane)
        wsprintfW(k, L"%s\\ShellEx\\%s", e.ext, SHELL_PREVIEW_GUID);
        RegSetSZ(HKEY_CLASSES_ROOT, k, NULL, e.clsid);
        // Extract image (legacy tools)
        wsprintfW(k, L"%s\\ShellEx\\%s", e.ext, SHELL_EXTRACT_GUID);
        RegSetSZ(HKEY_CLASSES_ROOT, k, NULL, e.clsid);
    }

    // Approved shell extensions
    static const wchar_t* APPROVED =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved";
    for (auto& e : cls)
        RegSetSZ(HKEY_LOCAL_MACHINE, APPROVED, e.clsid, e.label);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return S_OK;
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DllUnregisterServer(void) {
    const wchar_t* clsids[] = {
        CLSID_HDR_STR, CLSID_EXR_STR, CLSID_TGA_STR, CLSID_SVG_STR
    };
    for (auto c : clsids) {
        wchar_t k[256];
        wsprintfW(k, L"CLSID\\%s\\InprocServer32", c); RegDeleteKeyW(HKEY_CLASSES_ROOT, k);
        wsprintfW(k, L"CLSID\\%s",                 c); RegDeleteKeyW(HKEY_CLASSES_ROOT, k);
        HKEY hk;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
                0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
            RegDeleteValueW(hk, c); RegCloseKey(hk);
        }
    }
    const wchar_t* exts[] = { L".hdr", L".exr", L".tga", L".svg" };
    for (auto e : exts) {
        wchar_t k[256];
        wsprintfW(k, L"%s\\ShellEx\\%s", e, SHELL_THUMB_GUID);
        RegDeleteKeyW(HKEY_CLASSES_ROOT, k);
        wsprintfW(k, L"%s\\ShellEx\\%s", e, SHELL_PREVIEW_GUID);
        RegDeleteKeyW(HKEY_CLASSES_ROOT, k);
        wsprintfW(k, L"%s\\ShellEx\\%s", e, SHELL_EXTRACT_GUID);
        RegDeleteKeyW(HKEY_CLASSES_ROOT, k);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return S_OK;
}
