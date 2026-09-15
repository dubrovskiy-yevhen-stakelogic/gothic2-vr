@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install-release.ps1" %*
set "result=%errorlevel%"
if not "%result%"=="0" echo Installation failed. Read the error above.
pause
exit /b %result%
