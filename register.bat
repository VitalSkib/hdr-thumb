@echo off
net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Administrator rights required.
    echo Right-click register.bat and select "Run as administrator".
    pause
    exit /b 1
)

set DLL=%~dp0build\hdr_thumb.dll

if not exist "%DLL%" (
    echo [ERROR] build\hdr_thumb.dll not found. Run build.bat first.
    pause
    exit /b 1
)

echo [*] Registering %DLL% ...
regsvr32 /s "%DLL%"
if errorlevel 1 (
    echo [FAIL] regsvr32 returned an error.
    pause
    exit /b 1
)

echo [OK] DLL registered.
echo [*] Restarting Explorer...
taskkill /f /im explorer.exe >nul 2>&1
timeout /t 1 /nobreak >nul
start explorer.exe
echo [DONE] HDR and EXR thumbnails are now active.
pause
