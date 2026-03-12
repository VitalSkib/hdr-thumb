/**
 * HDR + EXR Thumbnail Provider for Windows Explorer
 *
 * One DLL, two COM classes:
 *   HdrThumbnailProvider  -> .hdr (Radiance) via stb_image
 *   ExrThumbnailProvider  -> .exr (OpenEXR)  via tinyexr
 *
 * Build:    build.bat
 * Register: register.bat (as Administrator)
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

#ifndef WTSAT_OPAQUE
#define WTSAT_OPAQUE ((WTS_ALPHATYPE)3)
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_HDR
#define STBI_NO_STDIO
#include "../include/stb_image.h"

#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 1
#include "../include/tinyexr.h"

// CLSIDs
static const CLSID CLSID_HdrThumbProvider = {
    0x6A7B3E40,0x1234,0x4321,{0xAB,0xCD,0x9F,0x0E,0x1D,0x2C,0x3B,0x4A}
};
static const wchar_t* CLSID_HDR_STR = L"{6A7B3E40-1234-4321-ABCD-9F0E1D2C3B4A}";

static const CLSID CLSID_ExrThumbProvider = {
    0x7B8C4F51,0x2345,0x5432,{0xBC,0xDE,0xA0,0x1F,0x2E,0x3D,0x4C,0x5B}
};
static const wchar_t* CLSID_EXR_STR = L"{7B8C4F51-2345-5432-BCDE-A01F2E3D4C5B}";

static const wchar_t* SHELL_THUMB_GUID = L"{e357fccd-a995-4576-b01f-234630154e96}";

static const IID IID_IThumbnailProvider_ = {
    0xe357fccd,0xa995,0x4576,{0xb0,0x1f,0x23,0x46,0x30,0x15,0x4e,0x96}
};
static const IID IID_IInitializeWithStream_ = {
    0xb824b49d,0x22ac,0x4161,{0xac,0x8a,0x99,0x16,0xe8,0xfa,0x3f,0x7f}
};

// Utilities
static inline float reinhard(float v) { return v / (1.f + v); }
static inline BYTE  toU8(float v) {
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    return (BYTE)(v*255.f+0.5f);
}
static inline float srgb(float v) { return powf(v < 0.f ? 0.f : v, 1.f/2.2f); }

static void bilerp(const float* src, int sw, int sh,
                   float* dst, int dw, int dh)
{
    float sx=(float)sw/dw, sy=(float)sh/dh;
    for (int dy=0;dy<dh;dy++) {
        float fy=(dy+.5f)*sy-.5f;
        int y0=(int)fy; if(y0<0)y0=0;
        int y1=y0+1;    if(y1>=sh)y1=sh-1;
        float wy=fy-y0;
        for (int dx=0;dx<dw;dx++) {
            float fx=(dx+.5f)*sx-.5f;
            int x0=(int)fx; if(x0<0)x0=0;
            int x1=x0+1;    if(x1>=sw)x1=sw-1;
            float wx=fx-x0;
            for (int c=0;c<3;c++)
                dst[(dy*dw+dx)*3+c]=
                    src[(y0*sw+x0)*3+c]*(1-wx)*(1-wy)+
                    src[(y0*sw+x1)*3+c]*   wx *(1-wy)+
                    src[(y1*sw+x0)*3+c]*(1-wx)*   wy +
                    src[(y1*sw+x1)*3+c]*   wx *   wy;
        }
    }
}

static HBITMAP FloatRGBtoHBitmap(const float* rgb, int w, int h)
{
    BITMAPINFO bmi={};
    bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=w; bmi.bmiHeader.biHeight=-h;
    bmi.bmiHeader.biPlanes=1; bmi.bmiHeader.biBitCount=32;
    bmi.bmiHeader.biCompression=BI_RGB;
    void* bits=NULL;
    HDC hdc=GetDC(NULL);
    HBITMAP hbm=CreateDIBSection(hdc,&bmi,DIB_RGB_COLORS,&bits,NULL,0);
    ReleaseDC(NULL,hdc);
    if(!hbm||!bits) return NULL;
    BYTE* pix=(BYTE*)bits;
    for(int i=0;i<w*h;i++){
        pix[i*4+0]=toU8(srgb(reinhard(rgb[i*3+2])));
        pix[i*4+1]=toU8(srgb(reinhard(rgb[i*3+1])));
        pix[i*4+2]=toU8(srgb(reinhard(rgb[i*3+0])));
        pix[i*4+3]=0xFF;
    }
    return hbm;
}

static void thumbSize(int w,int h,UINT cx,int&dw,int&dh){
    if(w>=h){dw=(int)cx;dh=std::max(1,(int)((float)cx*h/w));}
    else    {dh=(int)cx;dw=std::max(1,(int)((float)cx*w/h));}
}

// HDR decode
static HBITMAP DecodeHDR(const BYTE* data, DWORD size, UINT cx)
{
    int w,h,comp;
    float* hdr=stbi_loadf_from_memory(data,(int)size,&w,&h,&comp,3);
    if(!hdr) return NULL;
    int dw,dh; thumbSize(w,h,cx,dw,dh);
    float* sc=(float*)malloc(dw*dh*3*sizeof(float));
    if(!sc){stbi_image_free(hdr);return NULL;}
    bilerp(hdr,w,h,sc,dw,dh);
    stbi_image_free(hdr);
    HBITMAP hbm=FloatRGBtoHBitmap(sc,dw,dh);
    free(sc); return hbm;
}

// EXR decode — supports scanline and tiled EXR (tinyexr v0.9.5)
static HBITMAP DecodeEXR(const BYTE* data, DWORD /*size*/, UINT cx)
{
    EXRVersion version;
    if (ParseEXRVersionFromMemory(&version, data) != TINYEXR_SUCCESS)
        return NULL;

    EXRHeader header;
    InitEXRHeader(&header);
    const char* err = NULL;
    if (ParseEXRHeaderFromMemory(&header, &version, data, &err) != TINYEXR_SUCCESS)
        return NULL;

    for (int i=0; i<header.num_channels; i++)
        header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;

    EXRImage image;
    InitEXRImage(&image);
    if (LoadEXRImageFromMemory(&image, &header, data, &err) != TINYEXR_SUCCESS) {
        FreeEXRHeader(&header); return NULL;
    }

    int w=image.width, h=image.height;

    // Find R/G/B channel indices by name, fallback to index order
    int idxR=-1, idxG=-1, idxB=-1;
    for (int i=0; i<header.num_channels; i++) {
        const char* n=header.channels[i].name;
        if      (strcmp(n,"R")==0) idxR=i;
        else if (strcmp(n,"G")==0) idxG=i;
        else if (strcmp(n,"B")==0) idxB=i;
    }
    if (idxR<0) idxR=0;
    if (idxG<0) idxG=(header.num_channels>1)?1:0;
    if (idxB<0) idxB=(header.num_channels>2)?2:0;

    float* rgb=(float*)calloc(w*h*3, sizeof(float));
    if (!rgb) { FreeEXRImage(&image); FreeEXRHeader(&header); return NULL; }

    if (header.tiled && image.tiles) {
        // Tiled EXR: assemble tiles into contiguous buffer
        for (int t=0; t<image.num_tiles; t++) {
            const EXRTile& tile=image.tiles[t];
            int tx=tile.offset_x*header.tile_size_x;
            int ty=tile.offset_y*header.tile_size_y;
            int tw=tile.width, th=tile.height;
            float** tp=reinterpret_cast<float**>(tile.images);
            for (int py=0; py<th; py++) {
                int iy=ty+py; if(iy>=h) break;
                for (int px=0; px<tw; px++) {
                    int ix=tx+px; if(ix>=w) break;
                    int dst=(iy*w+ix)*3, src=py*tw+px;
                    rgb[dst+0]=tp[idxR][src];
                    rgb[dst+1]=tp[idxG][src];
                    rgb[dst+2]=tp[idxB][src];
                }
            }
        }
    } else {
        // Scanline EXR
        float** planes=reinterpret_cast<float**>(image.images);
        for (int i=0; i<w*h; i++) {
            rgb[i*3+0]=planes[idxR][i];
            rgb[i*3+1]=planes[idxG][i];
            rgb[i*3+2]=planes[idxB][i];
        }
    }

    FreeEXRImage(&image);
    FreeEXRHeader(&header);

    int dw,dh; thumbSize(w,h,cx,dw,dh);
    float* sc=(float*)malloc(dw*dh*3*sizeof(float));
    if (!sc) { free(rgb); return NULL; }
    bilerp(rgb,w,h,sc,dw,dh);
    free(rgb);
    HBITMAP hbm=FloatRGBtoHBitmap(sc,dw,dh);
    free(sc); return hbm;
}

// Base provider
class BaseThumbProvider : public IInitializeWithStream, public IThumbnailProvider {
public:
    BaseThumbProvider():m_cRef(1),m_pStream(NULL){}
    virtual ~BaseThumbProvider(){if(m_pStream){m_pStream->Release();m_pStream=NULL;}}

    STDMETHODIMP_(ULONG) AddRef() override {return InterlockedIncrement(&m_cRef);}
    STDMETHODIMP_(ULONG) Release() override {
        LONG r=InterlockedDecrement(&m_cRef); if(r==0)delete this; return r;
    }
    STDMETHODIMP Initialize(IStream* ps, DWORD) override {
        if(m_pStream)m_pStream->Release();
        m_pStream=ps; m_pStream->AddRef(); return S_OK;
    }
    STDMETHODIMP GetThumbnail(UINT cx, HBITMAP* ph, WTS_ALPHATYPE* pa) override {
        if(!ph||!pa) return E_POINTER;
        if(!m_pStream) return E_UNEXPECTED;
        *ph=NULL; *pa=WTSAT_OPAQUE;
        STATSTG st={}; HRESULT hr=m_pStream->Stat(&st,STATFLAG_NONAME);
        if(FAILED(hr)) return hr;
        DWORD size=(DWORD)st.cbSize.QuadPart;
        if(!size||size>512*1024*1024) return E_FAIL;
        BYTE* buf=(BYTE*)malloc(size); if(!buf) return E_OUTOFMEMORY;
        LARGE_INTEGER li={}; m_pStream->Seek(li,STREAM_SEEK_SET,NULL);
        ULONG read=0; hr=m_pStream->Read(buf,size,&read);
        if(FAILED(hr)||read!=size){free(buf);return E_FAIL;}
        HBITMAP hbm=Decode(buf,size,cx); free(buf);
        if(!hbm) return E_FAIL;
        *ph=hbm; return S_OK;
    }
protected:
    virtual HBITMAP Decode(const BYTE* d,DWORD s,UINT cx)=0;
    volatile LONG m_cRef;
    IStream* m_pStream;
};

class HdrThumbProvider : public BaseThumbProvider {
public:
    STDMETHODIMP QueryInterface(REFIID r,void**p) override {
        if(!p)return E_POINTER;
        if(IsEqualIID(r,IID_IUnknown)||IsEqualIID(r,IID_IInitializeWithStream_))
            *p=static_cast<IInitializeWithStream*>(this);
        else if(IsEqualIID(r,IID_IThumbnailProvider_))
            *p=static_cast<IThumbnailProvider*>(this);
        else{*p=NULL;return E_NOINTERFACE;}
        AddRef();return S_OK;
    }
protected:
    HBITMAP Decode(const BYTE*d,DWORD s,UINT cx) override{return DecodeHDR(d,s,cx);}
};

class ExrThumbProvider : public BaseThumbProvider {
public:
    STDMETHODIMP QueryInterface(REFIID r,void**p) override {
        if(!p)return E_POINTER;
        if(IsEqualIID(r,IID_IUnknown)||IsEqualIID(r,IID_IInitializeWithStream_))
            *p=static_cast<IInitializeWithStream*>(this);
        else if(IsEqualIID(r,IID_IThumbnailProvider_))
            *p=static_cast<IThumbnailProvider*>(this);
        else{*p=NULL;return E_NOINTERFACE;}
        AddRef();return S_OK;
    }
protected:
    HBITMAP Decode(const BYTE*d,DWORD s,UINT cx) override{return DecodeEXR(d,s,cx);}
};

template<class T>
class ClassFactory : public IClassFactory {
    volatile LONG m_cRef;
public:
    ClassFactory():m_cRef(1){}
    virtual ~ClassFactory(){}
    STDMETHODIMP QueryInterface(REFIID r,void**p) override {
        if(!p)return E_POINTER;
        if(IsEqualIID(r,IID_IUnknown)||IsEqualIID(r,IID_IClassFactory))
            {*p=static_cast<IClassFactory*>(this);AddRef();return S_OK;}
        *p=NULL;return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()  override{return InterlockedIncrement(&m_cRef);}
    STDMETHODIMP_(ULONG) Release() override{
        LONG r=InterlockedDecrement(&m_cRef);if(r==0)delete this;return r;
    }
    STDMETHODIMP CreateInstance(IUnknown*o,REFIID r,void**p) override {
        if(o)return CLASS_E_NOAGGREGATION;
        T* t=new(std::nothrow)T(); if(!t)return E_OUTOFMEMORY;
        HRESULT hr=t->QueryInterface(r,p); t->Release(); return hr;
    }
    STDMETHODIMP LockServer(BOOL) override{return S_OK;}
};

static HMODULE g_hModule=NULL;

extern "C" BOOL WINAPI DllMain(HMODULE hm,DWORD r,LPVOID){
    if(r==DLL_PROCESS_ATTACH){g_hModule=hm;DisableThreadLibraryCalls(hm);}
    return TRUE;
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DllGetClassObject(REFCLSID rc,REFIID ri,void**pv){
    if(!pv)return E_POINTER;
    if(IsEqualCLSID(rc,CLSID_HdrThumbProvider)){
        auto*p=new(std::nothrow)ClassFactory<HdrThumbProvider>();
        if(!p)return E_OUTOFMEMORY;
        HRESULT hr=p->QueryInterface(ri,pv);p->Release();return hr;
    }
    if(IsEqualCLSID(rc,CLSID_ExrThumbProvider)){
        auto*p=new(std::nothrow)ClassFactory<ExrThumbProvider>();
        if(!p)return E_OUTOFMEMORY;
        HRESULT hr=p->QueryInterface(ri,pv);p->Release();return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DllCanUnloadNow(void){return S_OK;}

static void RegSetSZ(HKEY root,const wchar_t*path,const wchar_t*val,const wchar_t*data){
    HKEY hk;
    RegCreateKeyExW(root,path,0,NULL,0,KEY_SET_VALUE|KEY_CREATE_SUB_KEY,NULL,&hk,NULL);
    RegSetValueExW(hk,val,0,REG_SZ,(const BYTE*)data,(DWORD)(wcslen(data)+1)*2);
    RegCloseKey(hk);
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DllRegisterServer(void){
    wchar_t dll[MAX_PATH];
    GetModuleFileNameW(g_hModule,dll,MAX_PATH);
    struct{const wchar_t*clsid;const wchar_t*label;}cls[]={
        {CLSID_HDR_STR,L"HDR Thumbnail Provider"},
        {CLSID_EXR_STR,L"EXR Thumbnail Provider"},
    };
    for(auto&e:cls){
        wchar_t k1[256],k2[256];
        wsprintfW(k1,L"CLSID\\%s",e.clsid);
        wsprintfW(k2,L"CLSID\\%s\\InprocServer32",e.clsid);
        RegSetSZ(HKEY_CLASSES_ROOT,k1,NULL,e.label);
        RegSetSZ(HKEY_CLASSES_ROOT,k2,NULL,dll);
        RegSetSZ(HKEY_CLASSES_ROOT,k2,L"ThreadingModel",L"Apartment");
    }
    struct{const wchar_t*ext;const wchar_t*clsid;}exts[]={
        {L".hdr",CLSID_HDR_STR},{L".exr",CLSID_EXR_STR},
    };
    for(auto&e:exts){
        wchar_t k[256];
        wsprintfW(k,L"%s\\ShellEx\\%s",e.ext,SHELL_THUMB_GUID);
        RegSetSZ(HKEY_CLASSES_ROOT,k,NULL,e.clsid);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,NULL,NULL);
    return S_OK;
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DllUnregisterServer(void){
    const wchar_t*clsids[]={CLSID_HDR_STR,CLSID_EXR_STR};
    for(auto c:clsids){
        wchar_t k[256];
        wsprintfW(k,L"CLSID\\%s\\InprocServer32",c);RegDeleteKeyW(HKEY_CLASSES_ROOT,k);
        wsprintfW(k,L"CLSID\\%s",c);RegDeleteKeyW(HKEY_CLASSES_ROOT,k);
    }
    const wchar_t*exts[]={L".hdr",L".exr"};
    for(auto e:exts){
        wchar_t k[256];
        wsprintfW(k,L"%s\\ShellEx\\%s",e,SHELL_THUMB_GUID);
        RegDeleteKeyW(HKEY_CLASSES_ROOT,k);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,NULL,NULL);
    return S_OK;
}
