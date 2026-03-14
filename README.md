# hdr-thumb

**HDR / EXR / TGA / SVG Thumbnail Provider for Windows Explorer**

Adds native thumbnail previews for `.hdr`, `.exr`, `.tga` and `.svg` files directly in Windows Explorer — no third-party software required.

![Windows 10/11](https://img.shields.io/badge/Windows-10%2F11-0078D4?logo=windows)
![License: MIT](https://img.shields.io/badge/License-MIT-green)
![Architecture: x64](https://img.shields.io/badge/arch-x64-lightgrey)

---

## Features

- `.hdr` — Radiance RGBE format via [stb_image](https://github.com/nothings/stb)
- `.exr` — OpenEXR scanline **and tiled** via [tinyexr](https://github.com/syoyo/tinyexr)
- `.tga` — Targa via [stb_image](https://github.com/nothings/stb)
- `.svg` — Full SVG 1.1 via [lunasvg](https://github.com/sammycage/lunasvg) — CSS, currentColor, gradients, transparent background
- Reinhard tone mapping + sRGB gamma for HDR/EXR
- Bilinear downscaling to any thumbnail size Windows requests
- Single DLL, no runtime dependencies, no installer
- File size limit: 512 MB per file

---

## Requirements

- Windows 10 or 11 (x64)
- Administrator rights for registration

---

## Installation (pre-built DLL)

1. Download `hdr_thumb.dll` from [Releases](../../releases)
2. Copy to `C:\Windows\System32\`
3. Open **Command Prompt as Administrator** and run:

```cmd
regsvr32 "C:\Windows\System32\hdr_thumb.dll"
```

4. Open any folder with `.hdr`, `.exr`, `.tga` or `.svg` files — thumbnails appear immediately.

If thumbnails do not appear, flush the cache:

```cmd
taskkill /f /im explorer.exe
del /f /q "%LocalAppData%\Microsoft\Windows\Explorer\thumbcache_*.db"
del /f /q "%LocalAppData%\Microsoft\Windows\Explorer\iconcache_*.db"
start explorer.exe
```

### Uninstall

```cmd
regsvr32 /u "C:\Windows\System32\hdr_thumb.dll"
del "C:\Windows\System32\hdr_thumb.dll"
```

---

## Building from source

### Requirements

- [MinGW-w64](https://www.mingw-w64.org/) with GCC 13+ (UCRT, x86_64)
- [CMake](https://cmake.org/) — for building lunasvg
- [Git](https://git-scm.com/) — for cloning lunasvg, or download ZIP manually

### Step 1 — Build lunasvg static library (once)

Download [lunasvg](https://github.com/sammycage/lunasvg) source (with the `plutovg` subfolder included) and place it at `C:\mingw64\lunasvg_src\`, then run:

```cmd
build_lunasvg.bat
```

This produces:
- `lib\liblunasvg.a`
- `include\lunasvg.h`

### Step 2 — Build the DLL

```cmd
build.bat
```

Output: `hdr_thumb.dll`

### Directory structure

```
hdr-thumb/
├── src/
│   └── hdr_thumb.cpp        — full source, single translation unit
├── include/
│   ├── stb_image.h          — stb_image (Public Domain)
│   ├── tinyexr.h            — tinyexr v0.9.5 (BSD 3-Clause)
│   └── lunasvg.h            — lunasvg header (MIT)
├── lib/
│   └── liblunasvg.a         — built by build_lunasvg.bat (not in repo)
├── build.bat                — main build script
├── build_lunasvg.bat        — builds lunasvg static library
└── README.md
```

> `lib\liblunasvg.a` is not included in the repository.
> Run `build_lunasvg.bat` once to generate it before building the DLL.

---

## How it works

Windows Shell calls `IThumbnailProvider::GetThumbnail()` when Explorer needs a preview.
The DLL reads the file stream and dispatches to the appropriate decoder:

- **HDR/EXR** — decoded to linear float RGB, Reinhard tone mapping applied, converted to sRGB
- **TGA** — decoded directly to 8-bit RGBA, no tone mapping
- **SVG** — rendered by lunasvg to premultiplied ARGB with transparent background

For tiled EXR files tiles are assembled into a contiguous buffer before scaling.
SVG thumbnails preserve transparency (`WTSAT_ARGB`), so Explorer composites them correctly over its own background.

---

## Third-party libraries

| Library | Author | License |
|---|---|---|
| [stb_image.h](https://github.com/nothings/stb) | Sean Barrett | Public Domain / MIT |
| [tinyexr.h](https://github.com/syoyo/tinyexr) | Syoyo Fujita | BSD 3-Clause |
| [lunasvg](https://github.com/sammycage/lunasvg) | Nwutobo Samuel Ugochukwu | MIT |

---

## Disclaimer

This software is provided **as-is**, without warranty of any kind.
Use it at your own discretion. No support is provided.

---

## License

MIT © 2026 VitalS — see [LICENSE](LICENSE)
