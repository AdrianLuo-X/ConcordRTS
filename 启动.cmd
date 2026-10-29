@echo off
setlocal
pushd "%~dp0" || exit /b 1
if not exist "%~dp0bin\concord_runtime.exe" (
  echo Missing local game executable. Run the local build entry first.
  popd
  exit /b 1
)
if "%CONCORD_LAUNCH_WAIT%"=="1" goto run_wait
start "" "%~dp0bin\concord_runtime.exe" --config "%~dp0local_data\runtime_config.ini" --resource-root "%~dp0RedAlert2" --local-data-root "%~dp0local_data" %*
set "concord_exit=%errorlevel%"
goto finish
:run_wait
"%~dp0bin\concord_runtime.exe" --config "%~dp0local_data\runtime_config.ini" --resource-root "%~dp0RedAlert2" --local-data-root "%~dp0local_data" %*
set "concord_exit=%errorlevel%"
:finish
popd
exit /b %concord_exit%