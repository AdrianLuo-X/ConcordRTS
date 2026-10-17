@echo off
setlocal
cd /d "%~dp0"
if "%CONCORD_LAUNCH_WAIT%"=="1" goto diagnostic_wait

start "" "%~dp0bin\concord_runtime.exe" --config "%~dp0local_data\runtime_config.ini" --resource-root "%~dp0RedAlert2" --local-data-root "%~dp0local_data" %*
exit /b

:diagnostic_wait
"%~dp0bin\concord_runtime.exe" --config "%~dp0local_data\runtime_config.ini" --resource-root "%~dp0RedAlert2" --local-data-root "%~dp0local_data" %*
exit /b %errorlevel%
