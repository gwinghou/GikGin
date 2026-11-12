@echo off
setlocal
cd /d "%~dp0"

if not exist "%~dp0GikGinIME.dll" (
    echo [ERROR] GikGinIME.dll was not found next to this script.
    pause
    exit /b 1
)
if not exist "%~dp0dict_pinyin.dat" (
    echo [ERROR] dict_pinyin.dat was not found next to this script.
    pause
    exit /b 1
)

fltmc >nul 2>&1
if errorlevel 1 (
    echo Administrator privileges are required. Raising elevation...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

echo Registering GikGin input method...
"%SystemRoot%\System32\regsvr32.exe" /s "%~dp0GikGinIME.dll"
if errorlevel 1 (
    echo [ERROR] Registration failed.
    pause
    exit /b 1
)

echo.
echo Installed successfully.
echo.
echo To start using it:
echo   1. Open Settings - Time ^& Language - Language.
echo   2. Select Chinese (Simplified) - Options - Add a keyboard.
echo   3. Choose "GikGin IME".
echo   4. Press Win+Space to switch input methods.
echo   5. Run GikGinIMESettings.exe (or click Settings in the
echo      candidate window) to choose Full Pinyin / Ziranma Shuangpin.
echo.
echo Keep GikGinIME.dll and dict_pinyin.dat in this folder;
echo the registration points at this path.
pause
