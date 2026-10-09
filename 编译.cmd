@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Build-Game.ps1"
if errorlevel 1 (
  echo Build failed. Review the error above.
  pause
  exit /b 1
)
echo Build complete.
pause
