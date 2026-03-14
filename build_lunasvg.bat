@echo off
::
:: build_lunasvg.bat
:: Builds lunasvg as a static library for MinGW (x64)
::
:: Location:  C:\hdr_thumb\build_lunasvg.bat
:: Sources:   C:\mingw64\lunasvg_src\   (already downloaded)
:: Output:    C:\hdr_thumb\lib\liblunasvg.a
::            C:\hdr_thumb\include\lunasvg.h
::

set "MINGW=C:\mingw64\bin"
set "SRC=C:\mingw64\lunasvg_src"
set "BUILD=%SRC%\build_mingw"
set "LIB_OUT=C:\hdr_thumb\lib"
set "INC_OUT=C:\hdr_thumb\include"

echo.
echo === [1/3] Configuring CMake ===

"%MINGW%\cmake.exe" -S "%SRC%" -B "%BUILD%" ^
    -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_SHARED_LIBS=OFF ^
    -DLUNASVG_BUILD_EXAMPLES=OFF ^
    -DCMAKE_C_COMPILER="%MINGW%\gcc.exe" ^
    -DCMAKE_CXX_COMPILER="%MINGW%\g++.exe" ^
    -DCMAKE_MAKE_PROGRAM="%MINGW%\mingw32-make.exe"

if %errorlevel% neq 0 (
    echo.
    echo  ERROR: CMake configuration failed.
    pause & exit /b 1
)

echo.
echo === [2/3] Building liblunasvg.a ===

"%MINGW%\cmake.exe" --build "%BUILD%" --config Release -- -j4

if %errorlevel% neq 0 (
    echo.
    echo  ERROR: Build failed.
    pause & exit /b 1
)

echo.
echo === [3/3] Copying files to project ===

if not exist "%LIB_OUT%" mkdir "%LIB_OUT%"
if not exist "%INC_OUT%" mkdir "%INC_OUT%"

if exist "%BUILD%\liblunasvg.a" (
    copy /y "%BUILD%\liblunasvg.a" "%LIB_OUT%\"
    echo   Copied: liblunasvg.a
) else if exist "%BUILD%\lunasvg\liblunasvg.a" (
    copy /y "%BUILD%\lunasvg\liblunasvg.a" "%LIB_OUT%\"
    echo   Copied: liblunasvg.a
) else (
    echo.
    echo  ERROR: liblunasvg.a not found after build!
    echo  Contents of build folder:
    dir "%BUILD%\*.a" /s /b 2>nul
    pause & exit /b 1
)

copy /y "%SRC%\include\lunasvg.h" "%INC_OUT%\"
echo   Copied: lunasvg.h

echo.
echo ============================================================
echo  DONE:
echo    %LIB_OUT%\liblunasvg.a
echo    %INC_OUT%\lunasvg.h
echo.
echo  You can now run build.bat
echo ============================================================
echo.
pause
