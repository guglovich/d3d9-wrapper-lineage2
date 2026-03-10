#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>

// ==========================================================
// Logging
// ==========================================================
static FILE* g_log       = nullptr;
static char  g_base_path[MAX_PATH] = {};

static void log_init(HINSTANCE h) {
    char p[MAX_PATH];
    GetModuleFileNameA(h, p, MAX_PATH);
    strncpy(g_base_path, p, MAX_PATH);
    char* slash = strrchr(g_base_path, '\\');
    if (slash) *(slash+1) = '\0';
    char* dot = strrchr(p, '.');
    if (dot) strcpy(dot, "_log.txt"); else strcat(p, "_log.txt");
    g_log = fopen(p, "w");
    if (g_log) { fprintf(g_log, "=== d3d9 proxy log ===\nlog: %s\n", p); fflush(g_log); }
}
static void log_write(const char* m) { if (!g_log) return; fprintf(g_log, "%s\n", m); fflush(g_log); }
static void log_close() { if (!g_log) return; fprintf(g_log, "proxy unloaded\n"); fflush(g_log); fclose(g_log); g_log = nullptr; }

// ==========================================================
// Key name -> virtual key code
// ==========================================================
static int key_name_to_vk(const char* name) {
    struct { const char* n; int vk; } table[] = {
        { "RightAlt",   0xA5 }, { "RightCtrl",  0xA3 },
        { "Home",       0x24 }, { "End",        0x23 },
        { "PageUp",     0x21 }, { "PageDown",   0x22 },
        { "Insert",     0x2D }, { "Delete",     0x2E },
        { "ScrollLock", 0x91 }, { "Pause",      0x13 },
    };
    for (auto& e : table)
        if (_stricmp(name, e.n) == 0) return e.vk;
    if (name[0] == '0' && (name[1] == 'x' || name[1] == 'X'))
        return (int)strtol(name, nullptr, 16);
    return 0;
}

// ==========================================================
// Config
// ==========================================================
struct Config {
    int   anisotropy      = 16;
    float lod_bias        = -1.0f;
    bool  lod_bias_apply  = true;
    int   fps_limit       = 0;
    int   fog_mode        = 0;
    float fog_start       = 4096.0f;
    float fog_end         = 16384.0f;
    DWORD fog_color       = 0x00C8D4E8;
    int   frame_latency   = 1;
    bool  detector        = true;
    bool  enhancements    = true;
    int   toggle_key      = 0xA5;
    int   detect_key      = 0xA3;
};
static Config g_cfg;

static int ini_int(const char* path, const char* sec, const char* key, int def) {
    return (int)GetPrivateProfileIntA(sec, key, def, path);
}
static float ini_float(const char* path, const char* sec, const char* key, float def) {
    char buf[32], defbuf[32];
    snprintf(defbuf, sizeof(defbuf), "%.4f", def);
    GetPrivateProfileStringA(sec, key, defbuf, buf, sizeof(buf), path);
    return (float)atof(buf);
}

static void config_load() {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%sd3d9_proxy.ini", g_base_path);
    FILE* f = fopen(path, "r");
    if (!f) {
        f = fopen(path, "w");
        if (f) {
            fprintf(f,
                "; d3d9 proxy for Lineage 2\n\n"
                "[fps]\n"
                "; FPS limit. 0 = disabled.\n"
                "limit=0\n\n"
                "[graphics]\n"
                "; Anisotropic filtering level (0/1 = off, 2-16 = on)\n"
                "anisotropy=16\n"
                "; Mipmap LOD bias. Negative = sharper mips. 0.0 = off.\n"
                "lod_bias=-1.0\n\n"
                "[fog]\n"
                "; 0=off  1=full override  2=blend (game color + proxy distances)\n"
                "mode=0\n"
                "; Fog distances in UE units (game defaults: start=8192 end=20480)\n"
                "start=4096.0\n"
                "end=16384.0\n"
                "; Fog color hex RGB, used in mode=1 only\n"
                "color=0xC8D4E8\n\n"
                "[latency]\n"
                "; Max frames GPU can queue ahead of CPU. Lower = less input lag.\n"
                "; 1 = minimum (recommended). 0 = use driver default (~3). Range: 0-16.\n"
                "; Applied via IDirect3DDevice9Ex::SetMaximumFrameLatency.\n"
                "; Works on DXVK/Wine. On native Windows also works if driver supports Ex.\n"
                "frame_latency=1\n\n"
                "[detector]\n"
                "; Log render state events to _log.txt\n"
                "enabled=1\n\n"
                "[hotkeys]\n"
                "; Toggle all enhancements on/off\n"
                "; Options: RightAlt, RightCtrl, Home, End, PageUp, PageDown,\n"
                ";          Insert, Delete, ScrollLock, Pause, or hex e.g. 0x77\n"
                "toggle=RightAlt\n"
                "; Force detector dump to log\n"
                "detect=RightCtrl\n"
            );
            fclose(f);
        }
        log_write("Config created: d3d9_proxy.ini");
    } else { fclose(f); }

    g_cfg.fps_limit      = ini_int(path,   "fps",      "limit",        0);
    g_cfg.anisotropy     = ini_int(path,   "graphics", "anisotropy",   16);
    g_cfg.lod_bias       = ini_float(path, "graphics", "lod_bias",     -1.0f);
    g_cfg.lod_bias_apply = (g_cfg.lod_bias != 0.0f);
    g_cfg.fog_mode       = ini_int(path,   "fog",      "mode",         0);
    g_cfg.fog_start      = ini_float(path, "fog",      "start",        4096.0f);
    g_cfg.fog_end        = ini_float(path, "fog",      "end",          16384.0f);
    {
        char buf[32] = "0xC8D4E8";
        GetPrivateProfileStringA("fog", "color", "0xC8D4E8", buf, sizeof(buf), path);
        g_cfg.fog_color = (DWORD)strtoul(buf, nullptr, 16);
    }
    g_cfg.frame_latency  = ini_int(path, "latency",  "frame_latency", 1);
    g_cfg.detector       = ini_int(path, "detector", "enabled",       1) != 0;

    char toggle_name[64] = "RightAlt";
    GetPrivateProfileStringA("hotkeys", "toggle", "RightAlt", toggle_name, sizeof(toggle_name), path);
    g_cfg.toggle_key = key_name_to_vk(toggle_name);
    char detect_name[64] = "RightCtrl";
    GetPrivateProfileStringA("hotkeys", "detect", "RightCtrl", detect_name, sizeof(detect_name), path);
    g_cfg.detect_key = key_name_to_vk(detect_name);

    char buf[256];
    snprintf(buf, sizeof(buf),
        "Config loaded: fps=%d aniso=%d lod=%.2f fog_mode=%d latency=%d detector=%d toggle=%s detect=%s",
        g_cfg.fps_limit, g_cfg.anisotropy, g_cfg.lod_bias,
        g_cfg.fog_mode, g_cfg.frame_latency, (int)g_cfg.detector,
        toggle_name, detect_name);
    log_write(buf);
}

// ==========================================================
// Frame latency via IDirect3DDevice9Ex::SetMaximumFrameLatency
//
// IDirect3DDevice9 does not expose SetMaximumFrameLatency.
// We QueryInterface for IDirect3DDevice9Ex — DXVK and Wine
// both support this even when the app used plain CreateDevice.
// This is exactly the approach used by DXVK internally.
//
// Effect: GPU can queue at most N frames ahead of CPU.
// frame_latency=1 means GPU processes each frame before CPU
// starts the next one — lowest possible input lag.
// ==========================================================
static void latency_apply(IDirect3DDevice9* dev) {
    if (g_cfg.frame_latency <= 0) return;

    // Stage 1: IDirect3DDevice9Ex::SetMaximumFrameLatency
    // Works on: DXVK/Wine (always), Windows (if driver respects it).
    // Note: some AMD drivers ignore this and cap at 3 internally.
    // {b18b10ce-2649-405a-870f-95f777d4313a}
    static const GUID IID_IDirect3DDevice9Ex = {
        0xb18b10ce, 0x2649, 0x405a,
        {0x87, 0x0f, 0x95, 0xf7, 0x77, 0xd4, 0x31, 0x3a}
    };
    IDirect3DDevice9Ex* ex = nullptr;
    HRESULT hr = dev->QueryInterface(IID_IDirect3DDevice9Ex, (void**)&ex);
    if (SUCCEEDED(hr) && ex) {
        hr = ex->SetMaximumFrameLatency((UINT)g_cfg.frame_latency);
        ex->Release();
        char buf[64];
        snprintf(buf, sizeof(buf), "SetMaximumFrameLatency(%d): 0x%X",
            g_cfg.frame_latency, (unsigned)hr);
        log_write(buf);
    } else {
        log_write("IDirect3DDevice9Ex not available: latency stage 1 skipped");
    }
    // Stage 2 (software fallback) is fps_post_present() in Present().
    // It sleeps after Present returns, keeping CPU from queuing frames
    // ahead of the GPU regardless of driver latency support.
}

// ==========================================================
// FPS limiter — post-Present sleep
//
// Sleep happens AFTER Present() returns, not before.
// With frame_latency=1, Present blocks until GPU is done with
// the previous frame, so we get precise timing feedback.
// CPU then sleeps the remaining frame budget — no busy-spin
// that wastes power and heats up the CPU unnecessarily.
// Last ~1ms is busy-waited for sub-millisecond precision.
// ==========================================================
static LARGE_INTEGER g_fps_freq   = {};
static LARGE_INTEGER g_fps_last   = {};
static bool          g_fps_init   = false;
static double        g_fps_budget = 0.0;

static void fps_init() {
    QueryPerformanceFrequency(&g_fps_freq);
    QueryPerformanceCounter(&g_fps_last);
    g_fps_init   = true;
    g_fps_budget = (g_cfg.fps_limit > 0) ? 1000000.0 / g_cfg.fps_limit : 0.0;
}

static void fps_post_present() {
    if (!g_fps_init || g_cfg.fps_limit <= 0 || !g_cfg.enhancements) {
        QueryPerformanceCounter(&g_fps_last);
        return;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double elapsed = (double)(now.QuadPart - g_fps_last.QuadPart)
                     * 1000000.0 / g_fps_freq.QuadPart;
    double remain  = g_fps_budget - elapsed;
    if (remain > 1000.0) {
        DWORD ms = (DWORD)((remain - 800.0) / 1000.0);
        if (ms > 0) Sleep(ms);
        do {
            QueryPerformanceCounter(&now);
            elapsed = (double)(now.QuadPart - g_fps_last.QuadPart)
                      * 1000000.0 / g_fps_freq.QuadPart;
        } while (elapsed < g_fps_budget);
    }
    QueryPerformanceCounter(&g_fps_last);
}

// ==========================================================
// Detector
// ==========================================================
struct DetectorState {
    bool  fog_seen = false;   DWORD fog_enable_val = 0;
    float fog_start = 0;      float fog_end = 0;
    DWORD fog_color = 0;      DWORD fog_table_mode = 0;
    DWORD fog_vertex_mode = 0;
    bool  lighting_seen = false; DWORD lighting_val = 0;
    int   set_light_count = 0;
    int   max_samplers = 0;   bool active_sampler[16] = {};
    int   flare_candidates = 0; bool alpha_blend_enabled = false;
};
static DetectorState g_det;

static void detector_dump() {
    if (!g_cfg.detector) return;
    log_write("=== DETECTOR DUMP ===");
    char buf[256];
    if (g_det.fog_seen) {
        snprintf(buf, sizeof(buf),
            "FOG: enable=%u table_mode=%u vertex_mode=%u start=%.0f end=%.0f color=0x%06X",
            g_det.fog_enable_val, g_det.fog_table_mode, g_det.fog_vertex_mode,
            g_det.fog_start, g_det.fog_end, g_det.fog_color & 0xFFFFFF);
        log_write(buf);
    } else log_write("FOG: not seen");
    snprintf(buf, sizeof(buf), "LIGHTING: D3DRS_LIGHTING=%u SetLight calls=%d",
        g_det.lighting_val, g_det.set_light_count); log_write(buf);
    snprintf(buf, sizeof(buf), "SAMPLERS: max simultaneous=%d", g_det.max_samplers); log_write(buf);
    snprintf(buf, sizeof(buf), "FLARE CANDIDATES: %d", g_det.flare_candidates); log_write(buf);
    log_write("=== END DUMP ===");
}

// ==========================================================
// Fog
// ==========================================================
static DWORD g_fog_game_color = 0x00F4FFB9;

static void fog_apply_full(IDirect3DDevice9* dev) {
    dev->SetRenderState(D3DRS_FOGENABLE,      TRUE);
    dev->SetRenderState(D3DRS_FOGCOLOR,       g_cfg.fog_color);
    dev->SetRenderState(D3DRS_FOGTABLEMODE,   D3DFOG_LINEAR);
    dev->SetRenderState(D3DRS_FOGVERTEXMODE,  D3DFOG_NONE);
    dev->SetRenderState(D3DRS_FOGSTART,       *(DWORD*)&g_cfg.fog_start);
    dev->SetRenderState(D3DRS_FOGEND,         *(DWORD*)&g_cfg.fog_end);
    dev->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
}
static void fog_apply_blend(IDirect3DDevice9* dev) {
    dev->SetRenderState(D3DRS_FOGENABLE,      TRUE);
    dev->SetRenderState(D3DRS_FOGCOLOR,       g_fog_game_color);
    dev->SetRenderState(D3DRS_FOGTABLEMODE,   D3DFOG_LINEAR);
    dev->SetRenderState(D3DRS_FOGVERTEXMODE,  D3DFOG_NONE);
    dev->SetRenderState(D3DRS_FOGSTART,       *(DWORD*)&g_cfg.fog_start);
    dev->SetRenderState(D3DRS_FOGEND,         *(DWORD*)&g_cfg.fog_end);
    dev->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
}
static void fog_reset(IDirect3DDevice9* dev) { dev->SetRenderState(D3DRS_FOGENABLE, FALSE); }

// ==========================================================
// Sampler states
// ==========================================================
static void sampler_apply(IDirect3DDevice9* dev) {
    for (DWORD s = 0; s < 8; s++) {
        dev->SetSamplerState(s, D3DSAMP_MAXANISOTROPY, (DWORD)g_cfg.anisotropy);
        dev->SetSamplerState(s, D3DSAMP_MINFILTER,     D3DTEXF_ANISOTROPIC);
        dev->SetSamplerState(s, D3DSAMP_MIPFILTER,     D3DTEXF_LINEAR);
        if (g_cfg.lod_bias_apply)
            dev->SetSamplerState(s, D3DSAMP_MIPMAPLODBIAS, *(DWORD*)&g_cfg.lod_bias);
    }
    log_write("Sampler states applied");
}
static void sampler_reset(IDirect3DDevice9* dev) {
    DWORD zero = 0;
    for (DWORD s = 0; s < 8; s++) {
        dev->SetSamplerState(s, D3DSAMP_MAXANISOTROPY, 1);
        dev->SetSamplerState(s, D3DSAMP_MINFILTER,     D3DTEXF_LINEAR);
        dev->SetSamplerState(s, D3DSAMP_MIPFILTER,     D3DTEXF_LINEAR);
        dev->SetSamplerState(s, D3DSAMP_MIPMAPLODBIAS, zero);
    }
    log_write("Sampler states reset");
}

// ==========================================================
// Hotkeys / window title
// ==========================================================
static HWND g_hwnd = nullptr;
static bool g_key_was_down = false, g_det_was_down = false;
static void hwnd_set(HWND h) { if (h && !g_hwnd) g_hwnd = h; }

static void hotkey_poll(IDirect3DDevice9* dev) {
    if (g_cfg.toggle_key) {
        bool down = (GetAsyncKeyState(g_cfg.toggle_key) & 0x8000) != 0;
        if (down && !g_key_was_down) {
            g_cfg.enhancements = !g_cfg.enhancements;
            if (g_cfg.enhancements) {
                sampler_apply(dev);
                if      (g_cfg.fog_mode == 1) fog_apply_full(dev);
                else if (g_cfg.fog_mode == 2) fog_apply_blend(dev);
            } else { sampler_reset(dev); fog_reset(dev); }
            if (g_hwnd) SetWindowTextA(g_hwnd, g_cfg.enhancements
                ? "Lineage II [Enhancements: ON]"
                : "Lineage II [Enhancements: OFF]");
            log_write(g_cfg.enhancements ? "Enhancements ON" : "Enhancements OFF");
        }
        g_key_was_down = down;
    }
    if (g_cfg.detect_key) {
        bool down = (GetAsyncKeyState(g_cfg.detect_key) & 0x8000) != 0;
        if (down && !g_det_was_down) detector_dump();
        g_det_was_down = down;
    }
}

// ==========================================================
// Globals
// ==========================================================
static HMODULE     real_d3d9   = nullptr;
static IDirect3D9* (WINAPI* real_Create9)(UINT) = nullptr;

#define D3DFMT_INTZ ((D3DFORMAT)MAKEFOURCC('I','N','T','Z'))
#define D3DFMT_DF24 ((D3DFORMAT)MAKEFOURCC('D','F','2','4'))
#define D3DFMT_DF16 ((D3DFORMAT)MAKEFOURCC('D','F','1','6'))

// ==========================================================
// Device proxy
// ==========================================================
struct DeviceProxy : IDirect3DDevice9 {
    IDirect3DDevice9* real;
    UINT sw = 1920, sh = 1080;
    IDirect3DSurface9* depth_ds  = nullptr;
    IDirect3DTexture9* depth_tex = nullptr;
    IDirect3DSurface9* depth_srf = nullptr;
    bool depth_ready = false;

    explicit DeviceProxy(IDirect3DDevice9* r) : real(r) { fps_init(); }
    ~DeviceProxy() { cleanup(); }

    void cleanup() {
        if (depth_srf) { depth_srf->Release(); depth_srf = nullptr; }
        if (depth_tex) { depth_tex->Release(); depth_tex = nullptr; }
        if (depth_ds)  { depth_ds->Release();  depth_ds  = nullptr; }
        depth_ready = false;
    }

    void init_depth(UINT w, UINT h) {
        if (depth_ready) return;
        IDirect3D9* d3d = nullptr; real->GetDirect3D(&d3d);
        D3DDEVICE_CREATION_PARAMETERS cp = {}; real->GetCreationParameters(&cp);
        D3DFORMAT fmt = D3DFMT_UNKNOWN; const char* name = "NONE";
        if (d3d) {
            auto chk = [&](D3DFORMAT f) {
                return SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType,
                    D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, f));
            };
            if      (chk(D3DFMT_DF24)) { fmt = D3DFMT_DF24; name = "DF24"; }
            else if (chk(D3DFMT_DF16)) { fmt = D3DFMT_DF16; name = "DF16"; }
            else if (chk(D3DFMT_INTZ)) { fmt = D3DFMT_INTZ; name = "INTZ"; }
            d3d->Release();
        }
        char buf[64]; snprintf(buf, sizeof(buf), "Readable depth fmt: %s", name); log_write(buf);
        if (fmt != D3DFMT_UNKNOWN) {
            HRESULT hr = real->CreateTexture(w, h, 1, D3DUSAGE_DEPTHSTENCIL,
                fmt, D3DPOOL_DEFAULT, &depth_tex, nullptr);
            snprintf(buf, sizeof(buf), "Depth tex create: 0x%X", (unsigned)hr); log_write(buf);
            if (SUCCEEDED(hr) && depth_tex) depth_tex->GetSurfaceLevel(0, &depth_srf);
        }
        depth_ready = true;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** p) override { return real->QueryInterface(r,p); }
    ULONG   STDMETHODCALLTYPE AddRef()  override { return real->AddRef(); }
    ULONG   STDMETHODCALLTYPE Release() override { ULONG r = real->Release(); if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override {
        HRESULT hr = real->TestCooperativeLevel();
        if (hr == D3DERR_DEVICELOST) cleanup();
        return hr;
    }
    UINT    STDMETHODCALLTYPE GetAvailableTextureMem()  override { return real->GetAvailableTextureMem(); }
    HRESULT STDMETHODCALLTYPE EvictManagedResources()   override { return real->EvictManagedResources(); }
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** p) override { return real->GetDirect3D(p); }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* p)  override { return real->GetDeviceCaps(p); }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT i, D3DDISPLAYMODE* p) override { return real->GetDisplayMode(i,p); }
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* p) override { return real->GetCreationParameters(p); }
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT x, UINT y, IDirect3DSurface9* s) override { return real->SetCursorProperties(x,y,s); }
    void    STDMETHODCALLTYPE SetCursorPosition(int x, int y, DWORD f) override { real->SetCursorPosition(x,y,f); }
    BOOL    STDMETHODCALLTYPE ShowCursor(BOOL b) override { return real->ShowCursor(b); }
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* p, IDirect3DSwapChain9** s) override { return real->CreateAdditionalSwapChain(p,s); }
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT i, IDirect3DSwapChain9** s) override { return real->GetSwapChain(i,s); }
    UINT    STDMETHODCALLTYPE GetNumberOfSwapChains() override { return real->GetNumberOfSwapChains(); }

    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* p) override {
        log_write("Reset"); cleanup();
        HRESULT hr = real->Reset(p);
        if (SUCCEEDED(hr) && p) {
            sw = p->BackBufferWidth  ? p->BackBufferWidth  : sw;
            sh = p->BackBufferHeight ? p->BackBufferHeight : sh;
            if (p->hDeviceWindow) hwnd_set(p->hDeviceWindow);
            if (g_cfg.enhancements) {
                sampler_apply(real);
                if      (g_cfg.fog_mode == 1) fog_apply_full(real);
                else if (g_cfg.fog_mode == 2) fog_apply_blend(real);
            }
            latency_apply(real);
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE Present(const RECT* a, const RECT* b, HWND c, const RGNDATA* d) override {
        hwnd_set(c);
        hotkey_poll(real);
        if (g_cfg.enhancements && g_cfg.fog_mode == 2) fog_apply_blend(real);
        HRESULT hr = real->Present(a, b, c, d);
        fps_post_present();
        return hr;
    }

    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT a, UINT b, D3DBACKBUFFER_TYPE c, IDirect3DSurface9** d) override { return real->GetBackBuffer(a,b,c,d); }
    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT a, D3DRASTER_STATUS* b) override { return real->GetRasterStatus(a,b); }
    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL b) override { return real->SetDialogBoxMode(b); }
    void    STDMETHODCALLTYPE SetGammaRamp(UINT a, DWORD b, const D3DGAMMARAMP* c) override { real->SetGammaRamp(a,b,c); }
    void    STDMETHODCALLTYPE GetGammaRamp(UINT a, D3DGAMMARAMP* b) override { real->GetGammaRamp(a,b); }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DTexture9** t, HANDLE* s) override { return real->CreateTexture(w,h,l,u,f,p,t,s); }
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT w, UINT h, UINT d, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DVolumeTexture9** t, HANDLE* s) override { return real->CreateVolumeTexture(w,h,d,l,u,f,p,t,s); }
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT s, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DCubeTexture9** t, HANDLE* sh) override { return real->CreateCubeTexture(s,l,u,f,p,t,sh); }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT l, DWORD u, DWORD f, D3DPOOL p, IDirect3DVertexBuffer9** v, HANDLE* s) override { return real->CreateVertexBuffer(l,u,f,p,v,s); }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DIndexBuffer9** i, HANDLE* s) override { return real->CreateIndexBuffer(l,u,f,p,i,s); }
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE m, DWORD q, BOOL l, IDirect3DSurface9** s, HANDLE* sh) override { return real->CreateRenderTarget(w,h,f,m,q,l,s,sh); }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE m, DWORD q, BOOL d, IDirect3DSurface9** s, HANDLE* sh) override {
        HRESULT hr = real->CreateDepthStencilSurface(w,h,f,m,q,d,s,sh);
        if (SUCCEEDED(hr) && w > 256 && h > 256 && !depth_ready) init_depth(w, h);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9* a, const RECT* b, IDirect3DSurface9* c, const POINT* d) override { return real->UpdateSurface(a,b,c,d); }
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9* a, IDirect3DBaseTexture9* b) override { return real->UpdateTexture(a,b); }
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* a, IDirect3DSurface9* b) override { return real->GetRenderTargetData(a,b); }
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT a, IDirect3DSurface9* b) override { return real->GetFrontBufferData(a,b); }
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* a, const RECT* b, IDirect3DSurface9* c, const RECT* d, D3DTEXTUREFILTERTYPE f) override { return real->StretchRect(a,b,c,d,f); }
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9* a, const RECT* b, D3DCOLOR c) override { return real->ColorFill(a,b,c); }
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT w, UINT h, D3DFORMAT f, D3DPOOL p, IDirect3DSurface9** s, HANDLE* sh) override { return real->CreateOffscreenPlainSurface(w,h,f,p,s,sh); }
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD i, IDirect3DSurface9* s) override { return real->SetRenderTarget(i,s); }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD i, IDirect3DSurface9** s) override { return real->GetRenderTarget(i,s); }
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* s) override {
        if (depth_ds) depth_ds->Release();
        depth_ds = s; if (s) s->AddRef();
        return real->SetDepthStencilSurface(s);
    }
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** s) override { return real->GetDepthStencilSurface(s); }
    HRESULT STDMETHODCALLTYPE BeginScene() override { return real->BeginScene(); }
    HRESULT STDMETHODCALLTYPE EndScene()   override { return real->EndScene(); }
    HRESULT STDMETHODCALLTYPE Clear(DWORD a, const D3DRECT* b, DWORD c, D3DCOLOR d, float e, DWORD f) override { return real->Clear(a,b,c,d,e,f); }
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE a, const D3DMATRIX* b) override { return real->SetTransform(a,b); }
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE a, D3DMATRIX* b) override { return real->GetTransform(a,b); }
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE a, const D3DMATRIX* b) override { return real->MultiplyTransform(a,b); }
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* p) override { return real->SetViewport(p); }
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* p) override { return real->GetViewport(p); }
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9* m) override { return real->SetMaterial(m); }
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9* m) override { return real->GetMaterial(m); }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD i, const D3DLIGHT9* l) override {
        if (g_cfg.detector) { g_det.set_light_count++; if (g_det.set_light_count == 1) log_write("DETECTOR: SetLight called"); }
        return real->SetLight(i,l);
    }
    HRESULT STDMETHODCALLTYPE GetLight(DWORD i, D3DLIGHT9* l) override { return real->GetLight(i,l); }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD i, BOOL b) override { return real->LightEnable(i,b); }
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD i, BOOL* b) override { return real->GetLightEnable(i,b); }
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD i, const float* p) override { return real->SetClipPlane(i,p); }
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD i, float* p) override { return real->GetClipPlane(i,p); }

    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE a, DWORD b) override {
        if (g_cfg.detector) {
            if (a == D3DRS_FOGENABLE) { if (!g_det.fog_seen) { g_det.fog_seen = true; log_write("DETECTOR: D3DRS_FOGENABLE seen"); } g_det.fog_enable_val = b; }
            if (a == D3DRS_FOGCOLOR)      g_det.fog_color       = b;
            if (a == D3DRS_FOGSTART)      g_det.fog_start       = *(float*)&b;
            if (a == D3DRS_FOGEND)        g_det.fog_end         = *(float*)&b;
            if (a == D3DRS_FOGTABLEMODE)  g_det.fog_table_mode  = b;
            if (a == D3DRS_FOGVERTEXMODE) g_det.fog_vertex_mode = b;
            if (a == D3DRS_LIGHTING) { if (!g_det.lighting_seen) { g_det.lighting_seen = true; log_write("DETECTOR: D3DRS_LIGHTING seen"); } g_det.lighting_val = b; }
            if (a == D3DRS_ALPHABLENDENABLE) g_det.alpha_blend_enabled = (b != 0);
        }
        if (a == D3DRS_FOGCOLOR) g_fog_game_color = b;
        if (g_cfg.enhancements && g_cfg.fog_mode > 0) {
            if (a == D3DRS_FOGSTART || a == D3DRS_FOGEND) return S_OK;
            if (a == D3DRS_FOGENABLE && g_cfg.fog_mode == 1) { fog_apply_full(real); return S_OK; }
        }
        return real->SetRenderState(a, b);
    }

    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE a, DWORD* b) override { return real->GetRenderState(a,b); }
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE t, IDirect3DStateBlock9** s) override { return real->CreateStateBlock(t,s); }
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override { return real->BeginStateBlock(); }
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9** s) override { return real->EndStateBlock(s); }
    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9* c) override { return real->SetClipStatus(c); }
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9* c) override { return real->GetClipStatus(c); }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD s, IDirect3DBaseTexture9** t) override { return real->GetTexture(s,t); }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD s, IDirect3DBaseTexture9* t) override {
        if (g_cfg.detector && s < 16) {
            g_det.active_sampler[s] = (t != nullptr);
            int cnt = 0; for (int i = 0; i < 16; i++) if (g_det.active_sampler[i]) cnt++;
            if (cnt > g_det.max_samplers) {
                g_det.max_samplers = cnt;
                char buf[64]; snprintf(buf, sizeof(buf), "DETECTOR: %d simultaneous samplers", cnt); log_write(buf);
            }
        }
        return real->SetTexture(s, t);
    }
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD s, D3DTEXTURESTAGESTATETYPE t, DWORD* v) override { return real->GetTextureStageState(s,t,v); }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD s, D3DTEXTURESTAGESTATETYPE t, DWORD v) override { return real->SetTextureStageState(s,t,v); }
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD s, D3DSAMPLERSTATETYPE t, DWORD* v) override { return real->GetSamplerState(s,t,v); }
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD s, D3DSAMPLERSTATETYPE t, DWORD v) override {
        if (g_cfg.enhancements && g_cfg.anisotropy > 1) {
            if (t == D3DSAMP_MAXANISOTROPY) v = (DWORD)g_cfg.anisotropy;
            if (t == D3DSAMP_MINFILTER)     v = D3DTEXF_ANISOTROPIC;
            if (t == D3DSAMP_MIPFILTER)     v = D3DTEXF_LINEAR;
        }
        if (g_cfg.enhancements && g_cfg.lod_bias_apply && t == D3DSAMP_MIPMAPLODBIAS)
            v = *(DWORD*)&g_cfg.lod_bias;
        return real->SetSamplerState(s, t, v);
    }
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* p) override { return real->ValidateDevice(p); }
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT p, const PALETTEENTRY* e) override { return real->SetPaletteEntries(p,e); }
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT p, PALETTEENTRY* e) override { return real->GetPaletteEntries(p,e); }
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT p) override { return real->SetCurrentTexturePalette(p); }
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT* p) override { return real->GetCurrentTexturePalette(p); }
    HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT* r) override { return real->SetScissorRect(r); }
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT* r) override { return real->GetScissorRect(r); }
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL b) override { return real->SetSoftwareVertexProcessing(b); }
    BOOL    STDMETHODCALLTYPE GetSoftwareVertexProcessing() override { return real->GetSoftwareVertexProcessing(); }
    HRESULT STDMETHODCALLTYPE SetNPatchMode(float n) override { return real->SetNPatchMode(n); }
    float   STDMETHODCALLTYPE GetNPatchMode() override { return real->GetNPatchMode(); }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE t, UINT a, UINT b) override {
        if (g_cfg.detector && g_det.alpha_blend_enabled) {
            bool small = (b == 2) && (t == D3DPT_TRIANGLELIST || t == D3DPT_TRIANGLESTRIP || t == D3DPT_TRIANGLEFAN);
            if (small && ++g_det.flare_candidates == 1) log_write("DETECTOR: flare/corona candidate seen");
        }
        return real->DrawPrimitive(t, a, b);
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT a, UINT b, UINT c, UINT d, UINT e) override { return real->DrawIndexedPrimitive(t,a,b,c,d,e); }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t, UINT c, const void* d, UINT s) override { return real->DrawPrimitiveUP(t,c,d,s); }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t, UINT a, UINT b, UINT c, const void* d, D3DFORMAT e, const void* f, UINT g) override { return real->DrawIndexedPrimitiveUP(t,a,b,c,d,e,f,g); }
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT a, UINT b, UINT c, IDirect3DVertexBuffer9* d, IDirect3DVertexDeclaration9* e, DWORD f) override { return real->ProcessVertices(a,b,c,d,e,f); }
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9* e, IDirect3DVertexDeclaration9** d) override { return real->CreateVertexDeclaration(e,d); }
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* d) override { return real->SetVertexDeclaration(d); }
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** d) override { return real->GetVertexDeclaration(d); }
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD f) override { return real->SetFVF(f); }
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD* f) override { return real->GetFVF(f); }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* f, IDirect3DVertexShader9** s) override { return real->CreateVertexShader(f,s); }
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* s) override { return real->SetVertexShader(s); }
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** s) override { return real->GetVertexShader(s); }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT r, const float* d, UINT c) override { return real->SetVertexShaderConstantF(r,d,c); }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT r, float* d, UINT c) override { return real->GetVertexShaderConstantF(r,d,c); }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT r, const int* d, UINT c) override { return real->SetVertexShaderConstantI(r,d,c); }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT r, int* d, UINT c) override { return real->GetVertexShaderConstantI(r,d,c); }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT r, const BOOL* d, UINT c) override { return real->SetVertexShaderConstantB(r,d,c); }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT r, BOOL* d, UINT c) override { return real->GetVertexShaderConstantB(r,d,c); }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT n, IDirect3DVertexBuffer9* v, UINT o, UINT s) override { return real->SetStreamSource(n,v,o,s); }
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT n, IDirect3DVertexBuffer9** v, UINT* o, UINT* s) override { return real->GetStreamSource(n,v,o,s); }
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT n, UINT d) override { return real->SetStreamSourceFreq(n,d); }
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT n, UINT* d) override { return real->GetStreamSourceFreq(n,d); }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* i) override { return real->SetIndices(i); }
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** i) override { return real->GetIndices(i); }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* f, IDirect3DPixelShader9** s) override { return real->CreatePixelShader(f,s); }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* s) override { return real->SetPixelShader(s); }
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** s) override { return real->GetPixelShader(s); }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT r, const float* d, UINT c) override { return real->SetPixelShaderConstantF(r,d,c); }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT r, float* d, UINT c) override { return real->GetPixelShaderConstantF(r,d,c); }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT r, const int* d, UINT c) override { return real->SetPixelShaderConstantI(r,d,c); }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT r, int* d, UINT c) override { return real->GetPixelShaderConstantI(r,d,c); }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT r, const BOOL* d, UINT c) override { return real->SetPixelShaderConstantB(r,d,c); }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT r, BOOL* d, UINT c) override { return real->GetPixelShaderConstantB(r,d,c); }
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT h, const float* s, const D3DRECTPATCH_INFO* i) override { return real->DrawRectPatch(h,s,i); }
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT h, const float* s, const D3DTRIPATCH_INFO* i) override { return real->DrawTriPatch(h,s,i); }
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT h) override { return real->DeletePatch(h); }
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE t, IDirect3DQuery9** q) override { return real->CreateQuery(t,q); }
};

// ==========================================================
// IDirect3D9 proxy
// ==========================================================
struct D3DProxy : IDirect3D9 {
    IDirect3D9* real;
    explicit D3DProxy(IDirect3D9* r) : real(r) { log_write("IDirect3D9 created"); }
    ~D3DProxy() { log_write("IDirect3D9 destroyed"); }

    HRESULT  STDMETHODCALLTYPE QueryInterface(REFIID r, void** p) override { return real->QueryInterface(r,p); }
    ULONG    STDMETHODCALLTYPE AddRef()  override { return real->AddRef(); }
    ULONG    STDMETHODCALLTYPE Release() override { ULONG r = real->Release(); if (!r) delete this; return r; }
    HRESULT  STDMETHODCALLTYPE RegisterSoftwareDevice(void* p) override { return real->RegisterSoftwareDevice(p); }
    UINT     STDMETHODCALLTYPE GetAdapterCount() override { return real->GetAdapterCount(); }
    HRESULT  STDMETHODCALLTYPE GetAdapterIdentifier(UINT a, DWORD b, D3DADAPTER_IDENTIFIER9* c) override { return real->GetAdapterIdentifier(a,b,c); }
    UINT     STDMETHODCALLTYPE GetAdapterModeCount(UINT a, D3DFORMAT b) override { return real->GetAdapterModeCount(a,b); }
    HRESULT  STDMETHODCALLTYPE EnumAdapterModes(UINT a, D3DFORMAT b, UINT c, D3DDISPLAYMODE* d) override { return real->EnumAdapterModes(a,b,c,d); }
    HRESULT  STDMETHODCALLTYPE GetAdapterDisplayMode(UINT a, D3DDISPLAYMODE* b) override { return real->GetAdapterDisplayMode(a,b); }
    HRESULT  STDMETHODCALLTYPE CheckDeviceType(UINT a, D3DDEVTYPE b, D3DFORMAT c, D3DFORMAT d, BOOL e) override { return real->CheckDeviceType(a,b,c,d,e); }
    HRESULT  STDMETHODCALLTYPE CheckDeviceFormat(UINT a, D3DDEVTYPE b, D3DFORMAT c, DWORD d, D3DRESOURCETYPE e, D3DFORMAT f) override { return real->CheckDeviceFormat(a,b,c,d,e,f); }
    HRESULT  STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT a, D3DDEVTYPE b, D3DFORMAT c, BOOL d, D3DMULTISAMPLE_TYPE e, DWORD* f) override { return real->CheckDeviceMultiSampleType(a,b,c,d,e,f); }
    HRESULT  STDMETHODCALLTYPE CheckDepthStencilMatch(UINT a, D3DDEVTYPE b, D3DFORMAT c, D3DFORMAT d, D3DFORMAT e) override { return real->CheckDepthStencilMatch(a,b,c,d,e); }
    HRESULT  STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT a, D3DDEVTYPE b, D3DFORMAT c, D3DFORMAT d) override { return real->CheckDeviceFormatConversion(a,b,c,d); }
    HRESULT  STDMETHODCALLTYPE GetDeviceCaps(UINT a, D3DDEVTYPE b, D3DCAPS9* c) override { return real->GetDeviceCaps(a,b,c); }
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT a) override { return real->GetAdapterMonitor(a); }

    HRESULT STDMETHODCALLTYPE CreateDevice(UINT a, D3DDEVTYPE b, HWND c, DWORD d,
        D3DPRESENT_PARAMETERS* e, IDirect3DDevice9** f) override {
        log_write("CreateDevice");
        if (e) {
            char buf[128];
            snprintf(buf, sizeof(buf), "PP: %ux%u fmt=0x%X interval=0x%X",
                e->BackBufferWidth, e->BackBufferHeight,
                (unsigned)e->BackBufferFormat, (unsigned)e->PresentationInterval);
            log_write(buf);
            hwnd_set(e->hDeviceWindow ? e->hDeviceWindow : c);

            // Log the game's vsync preference for diagnostics
            char ibuf[64];
            snprintf(ibuf, sizeof(ibuf), "PresentationInterval: 0x%X",
                (unsigned)e->PresentationInterval);
            log_write(ibuf);
        }
        HRESULT hr = real->CreateDevice(a,b,c,d,e,f);
        if (SUCCEEDED(hr) && f && *f) {
            auto* px = new DeviceProxy(*f);
            if (e) { px->sw = e->BackBufferWidth ? e->BackBufferWidth : 1920; px->sh = e->BackBufferHeight ? e->BackBufferHeight : 1080; }
            if (g_cfg.enhancements) {
                sampler_apply(px->real);
                if      (g_cfg.fog_mode == 1) fog_apply_full(px->real);
                else if (g_cfg.fog_mode == 2) fog_apply_blend(px->real);
            }
            latency_apply(px->real);
            *f = px;
            log_write("Device hooks installed");
        }
        return hr;
    }
};

// ==========================================================
// DLL entry + export
// ==========================================================
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        log_init(h); log_write("proxy loaded"); config_load();
    } else if (reason == DLL_PROCESS_DETACH) {
        detector_dump(); log_close();
        if (real_d3d9) { FreeLibrary(real_d3d9); real_d3d9 = nullptr; }
    }
    return TRUE;
}

extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT sdk) {
    log_write("Direct3DCreate9");
    if (!real_d3d9) {
        char sys[MAX_PATH];
        GetSystemDirectoryA(sys, MAX_PATH);
        strcat(sys, "\\d3d9.dll");
        real_d3d9 = LoadLibraryA(sys);
        if (!real_d3d9) { log_write("ERROR: cannot load real d3d9.dll"); return nullptr; }
        real_Create9 = (IDirect3D9*(WINAPI*)(UINT))GetProcAddress(real_d3d9, "Direct3DCreate9");
        if (!real_Create9) { log_write("ERROR: Direct3DCreate9 not found"); return nullptr; }
        log_write("real d3d9 loaded");
    }
    IDirect3D9* r = real_Create9(sdk);
    if (!r) { log_write("ERROR: null IDirect3D9"); return nullptr; }
    return new D3DProxy(r);
}
