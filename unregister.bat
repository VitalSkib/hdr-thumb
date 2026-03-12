@echo off
net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Administrator rights required.
    echo Right-click unregister.bat and select "Run as administrator".
    pause
    exit /b 1
)

set DLL=%~dp0build\hdr_thumb.dll

echo [*] Unregistering...
if exist "%DLL%" (
    regsvr32 /s /u "%DLL%"
    echo [OK] DLL unregistered.
) else (
    echo [!] DLL not found, cleaning registry manually...
    reg delete "HKCR\CLSID\{6A7B3E40-1234-4321-ABCD-9F0E1D2C3B4A}" /f >nul 2>&1
    reg delete "HKCR\CLSID\{7B8C4F51-2345-5432-BCDE-A01F2E3D4C5B}" /f >nul 2>&1
    reg delete "HKCR\.hdr\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}" /f >nul 2>&1
    reg delete "HKCR\.exr\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}" /f >nul 2>&1
)

echo [*] Restarting Explorer...
taskkill /f /im explorer.exe >nul 2>&1
timeout /t 1 /nobreak >nul
start explorer.exe
echo [DONE] Removed. No traces left in the system.
pause
