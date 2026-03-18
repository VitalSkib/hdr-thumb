@echo off
::
:: build.bat — builds hdr_thumb.dll
:: Location: C:\hdr_thumb\build.bat
::

set "MINGW=C:\mingw64\bin"
set "SRC=C:\hdr_thumb\src\hdr_thumb.cpp"
set "OUT=C:\hdr_thumb\hdr_thumb.dll"
set "INC=C:\hdr_thumb\include"
set "LUNASVG_LIB=C:\hdr_thumb\lib\liblunasvg.a"
set "PLUTOVG_LIB=C:\mingw64\lunasvg_src\build_mingw\plutovg\libplutovg.a"

if not exist "%LUNASVG_LIB%" (
    echo ERROR: %LUNASVG_LIB% not found. Run build_lunasvg.bat first.
    pause & exit /b 1
)

if not exist "%PLUTOVG_LIB%" (
    echo ERROR: %PLUTOVG_LIB% not found. Run build_lunasvg.bat first.
    pause & exit /b 1
)

if not exist "%SRC%" (
    echo ERROR: %SRC% not found.
    pause & exit /b 1
)

echo.
echo === Building hdr_thumb.dll ===
echo.

"%MINGW%\g++.exe" -std=c++17 -O2 -Wall ^
    -I "%INC%" ^
    -DLUNASVG_BUILD_STATIC ^
    -shared -o "%OUT%" "%SRC%" ^
    "%LUNASVG_LIB%" "%PLUTOVG_LIB%" ^
    -lshlwapi -lshell32 -lole32 -luuid -lgdi32 -lmsimg32 ^
    -static-libgcc -static-libstdc++ -static ^
    -Wl,-Bdynamic -lucrtbase -Wl,-Bstatic ^
    -Wl,--kill-at

if %errorlevel% neq 0 (
    echo.
    echo BUILD FAILED
    echo.
    pause & exit /b 1
)

echo.
echo BUILD OK: %OUT%
echo.
pause
