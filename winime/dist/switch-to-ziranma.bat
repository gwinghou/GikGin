@echo off
setlocal

echo Switching to Ziranma Shuangpin...
if exist "%~dp0GikGinIMESettings.exe" (
    start "" /wait "%~dp0GikGinIMESettings.exe" --ziranma
    if not errorlevel 1 goto ok
)

echo Settings program failed, trying reg.exe...
"%SystemRoot%\System32\reg.exe" add "HKCU\Software\GikGinIME" /v InputScheme /t REG_DWORD /d 1 /f >nul
if errorlevel 1 (
    echo [ERROR] Cannot save settings.
    echo Right-click this file and choose "Run as administrator", then retry.
    pause
    exit /b 1
)

:ok
echo Done: Ziranma Shuangpin.
echo Restart any app that is already open, then type nihk to test.
pause
