@echo off
setlocal

set CXX=g++

where %CXX% >nul 2>&1
if errorlevel 1 (
    echo [ERROR] g++ not found in PATH.
    echo Add C:\mingw64\bin to PATH and reopen cmd.
    exit /b 1
)

if not exist "include\stb_image.h" (
    echo [ERROR] include\stb_image.h not found.
    echo Download: https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
    exit /b 1
)

if not exist "include\tinyexr.h" (
    echo [ERROR] include\tinyexr.h not found.
    echo Download: https://raw.githubusercontent.com/syoyo/tinyexr/master/tinyexr.h
    exit /b 1
)

if not exist "build" mkdir build

echo [*] Compiling hdr_thumb.dll ...

%CXX% -std=c++17 -O2 -shared ^
    -o build\hdr_thumb.dll ^
    src\hdr_thumb.cpp ^
    src\hdr_thumb.def ^
    -I include ^
    -lole32 -loleaut32 -luuid -lgdi32 -lshlwapi -lshell32 ^
    -static-libgcc -static-libstdc++ ^
    -static ^
    -Wall -Wno-unused-function

if errorlevel 1 (
    echo [FAIL] Build failed.
    exit /b 1
)

echo [OK] build\hdr_thumb.dll ready.
echo Run register.bat as Administrator to install.
endlocal
