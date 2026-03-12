# hdr-thumb

**HDR + EXR Thumbnail Provider for Windows Explorer**

Adds native thumbnail previews for `.hdr` (Radiance) and `.exr` (OpenEXR) files directly in Windows Explorer — no third-party software required.

![Windows 10/11](https://img.shields.io/badge/Windows-10%2F11-0078D4?logo=windows)
![License: MIT](https://img.shields.io/badge/License-MIT-green)
![Architecture: x64](https://img.shields.io/badge/arch-x64-lightgrey)

---

## Features

- `.hdr` — Radiance RGBE format via [stb_image](https://github.com/nothings/stb)
- `.exr` — OpenEXR scanline **and tiled** via [tinyexr](https://github.com/syoyo/tinyexr)
- Reinhard tone mapping + sRGB gamma (2.2) for correct HDR-to-screen conversion
- Bilinear downscaling to any thumbnail size Windows requests
- Single DLL, no runtime dependencies, no installer required
- Supports files up to 512 MB

---

## Requirements

- Windows 10 or 11 (x64)
- Administrator rights for registration

---

## Installation (pre-built DLL)

1. Download `hdr_thumb.dll` from [Releases](../../releases)
2. Copy it anywhere permanent, e.g. `C:\Windows\System32\`
3. Open **Command Prompt as Administrator** and run:

```cmd
regsvr32 "C:\Windows\System32\hdr_thumb.dll"
```

4. Open any folder with `.hdr` or `.exr` files — thumbnails appear immediately.

### Uninstall

```cmd
regsvr32 /u "C:\Windows\System32\hdr_thumb.dll"
```

---

## Building from source

### Requirements

- [MinGW-w64](https://www.mingw-w64.org/) with GCC 13+ (UCRT, x86_64)
- Windows SDK headers (included with MinGW)

### Steps

```cmd
git clone https://github.com/YOUR_USERNAME/hdr-thumb.git
cd hdr-thumb
build.bat
```

Output: `build\hdr_thumb.dll`

### Directory structure

```
hdr-thumb/
├── src/
│   └── hdr_thumb.cpp       — full source, single translation unit
├── include/
│   ├── stb_image.h         — stb_image (Public Domain)
│   └── tinyexr.h           — tinyexr v0.9.5 (BSD 3-Clause)
├── build.bat               — MinGW build script
├── register.bat            — quick register helper (run as Admin)
└── README.md
```

---

## How it works

Windows Shell calls `IThumbnailProvider::GetThumbnail()` when Explorer needs a preview.
The DLL reads the file stream, decodes it to linear float RGB, applies
Reinhard tone mapping and sRGB gamma, then returns an `HBITMAP` to the shell.

For tiled EXR files the tiles are assembled into a contiguous buffer before scaling.

---

## Third-party libraries

| Library                                        | Author       | License             |
| ---------------------------------------------- | ------------ | ------------------- |
| [stb_image.h](https://github.com/nothings/stb) | Sean Barrett | Public Domain / MIT |
| [tinyexr.h](https://github.com/syoyo/tinyexr)  | Syoyo Fujita | BSD 3-Clause        |

---

## Disclaimer

This software is provided **as-is**, without warranty of any kind.
Use it at your own discretion. No support is provided.

---

## License

MIT © 2026 VitalS — see [LICENSE](LICENSE)
