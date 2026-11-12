@echo off
setlocal
cd /d "%~dp0"

echo Replacing GikGinIME.dll ...
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\replace-ime-dll.ps1" %*

set "RESULT=%ERRORLEVEL%"
echo.
if not "%RESULT%"=="0" (
    echo [ERROR] Replace failed with exit code %RESULT%.
)
pause
exit /b %RESULT%
