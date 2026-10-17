@echo off
cd /d "%~dp0"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Build-Game.ps1" %*
if errorlevel 1 (
  echo Build failed. Review the error above.
  if not "%CONCORD_BUILD_NO_PAUSE%"=="1" pause
  exit /b 1
)
echo Build complete.
if not "%CONCORD_BUILD_NO_PAUSE%"=="1" pause
