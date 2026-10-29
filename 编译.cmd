@echo off
setlocal
pushd "%~dp0" || exit /b 1
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Build-Game.ps1" %*
set "concord_exit=%errorlevel%"
popd
exit /b %concord_exit%