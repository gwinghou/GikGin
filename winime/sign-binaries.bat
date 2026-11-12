@echo off
setlocal
cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\sign-binaries.ps1" %*
set "RESULT=%ERRORLEVEL%"
echo.
if not "%RESULT%"=="0" (
    echo [ERROR] Signing failed with exit code %RESULT%.
)
pause
exit /b %RESULT%
