[Русская версия](README_RU.md)

# d3d9 wrapper for Lineage 2

A proxy `d3d9.dll` for Lineage 2 clients based on Unreal Engine 2 / Direct3D 9.  
Drop into the `system` folder — no installer, no runtime dependencies.  
Configuration via `d3d9_proxy.ini`, created automatically on first run.

Tested on **The 1st Throne: The Kamael**. Compatible with other chronicles on the same engine.

---

## Features

**Anisotropic filtering x16 + LOD bias**  
Forces AF on all samplers and sharpens mipmaps. Noticeably cleaner textures at close range without any post-processing.

**FPS limiter**  
Post-Present sleep with ~0.1ms precision. Does not spin the CPU.

**Fog override**  
Mode 1: full override — color and distances from INI.  
Mode 2: keep game fog color, override distances only.

**Frame latency**  
Calls `IDirect3DDevice9Ex::SetMaximumFrameLatency(1)` to reduce GPU queue depth. Lower input lag.  
On Wine + DXVK always works. On native Windows depends on driver — some AMD drivers accept the call but ignore it.

**Hotkey**  
RightAlt toggles all enhancements on/off at runtime. Window title shows current state.

---

## Compatibility

| Feature | Windows | Linux — Gallium Nine | Linux — DXVK |
|---|:---:|:---:|:---:|
| AF x16 + LOD bias | ✅ | ✅ | ✅ |
| FPS limiter | ✅ | ✅ | ✅ |
| Fog override | ✅ | ✅ | ✅ |
| Frame latency | ⚠️ driver | ✅ | ✅ |

---

## Installation

1. Copy `d3d9.dll` into the game's `system` folder (next to `l2.exe`)
2. Launch the game — `d3d9_proxy.ini` is created automatically
3. Edit the INI to configure, restart to apply

Compatible with ReShade when loaded via `opengl32.dll` (standard UE2 setup — no naming conflict).

---

## Screenshots

<img width="1920" height="1080" alt="image-comparison(1)" src="https://github.com/user-attachments/assets/a08ff500-c18e-40f4-aea2-bec4c4dbbe7a" />
<img width="1920" height="1080" alt="image-comparison" src="https://github.com/user-attachments/assets/b96933cd-45cc-414a-8476-72ce11bc0a14" />


---

## Build

### Linux (MinGW → Windows DLL)

```bash
i686-w64-mingw32-g++ -shared -o d3d9.dll d3d9_l2.cpp \
  -I/usr/i686-w64-mingw32/include \
  -ldxguid \
  -Wl,--enable-stdcall-fixup \
  -Wl,--add-stdcall-alias \
  -Wl,--kill-at \
  -static-libgcc \
  -static-libstdc++ \
  -std=c++17
```

### Windows (MinGW)

```bash
i686-w64-mingw32-g++ -shared -o d3d9.dll d3d9_l2.cpp \
  -ldxguid \
  -Wl,--kill-at \
  -static-libgcc \
  -static-libstdc++ \
  -std=c++17
```

### Windows (MSVC)

1. Create a new **DLL** project (x86 / Win32)
2. Add `d3d9_l2.cpp`
3. Linker → Input → Additional Dependencies: `d3d9.lib`, `dxguid.lib`
4. Build **Release x86**

---

## For developers

Full `IDirect3D9` / `IDirect3DDevice9` vtable wrapper. All methods delegate to the real device. No external dependencies beyond system d3d9.

**Implemented:**
- `SetRenderState` / `SetSamplerState` / `Present` / `Reset` interception
- `IDirect3DDevice9Ex` via QI on the live device — not a separate wrapper. On DXVK always succeeds regardless of `CreateDevice` type. On native Windows may return `E_NOINTERFACE` on older drivers; some AMD drivers accept but ignore. Both handled gracefully.
- Render state detector: fog params, lighting state, sampler count, flare candidates — dumps to log on exit or hotkey
- Depth texture creation (`DF24` / `DF16` / `INTZ`) probed at startup
- `sampler_apply` covers 8 samplers (detector recorded max 5 simultaneous in L2 Interlude)

**Can be added on top:**
- Draw call interception for geometry analysis
- Shader replacement via `CreatePixelShader` / `CreateVertexShader`
- HUD overlay via `DrawPrimitiveUP` in `EndScene`
- Depth buffer read (blocked on DXVK by design; works on Gallium Nine and native Windows)
- Frame timing stats, render target transition logging

Based on [d3d9-depth-proxy](https://github.com/guglovich/d3d9-depth-proxy). Key difference: `IDirect3DDevice9Ex` obtained via QI on the live device rather than a separate wrapper; `sampler_apply` covers 8 samplers instead of 16.
