@echo off
rem ============================================================
rem Sync working tree -> GikGinIME-source
rem Mirror ALL source files (winime/, src/, include/, data/,
rem tools/, CMakeLists.txt ...), skipping build artifacts.
rem Run this AFTER you finish editing code in the main tree.
rem ============================================================

robocopy "F:\aispace\libgooglepinyin\libgooglepinyin" "F:\aispace\libgooglepinyin\libgooglepinyin\GikGinIME-source" /MIR /NFL /NDL /NJH /NP /XD .git .vs build out GikGinIME-source /XF winime.7z GikGinIME-x64.zip *.user *.pdb GikGinIME.dll GikGinIMESettings.exe

if errorlevel 8 (
  echo.
  echo SYNC FAILED ^(robocopy exit code %errorlevel%^)
  pause
  exit /b 1
)

echo.
echo Sync finished. GikGinIME-source is up to date.
if "%1"=="" pause
