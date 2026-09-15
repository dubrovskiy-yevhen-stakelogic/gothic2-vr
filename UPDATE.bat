@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install-release.ps1" -UpdateOnly %*
set "result=%errorlevel%"
if not "%result%"=="0" echo Update failed. Read the error above.
pause
exit /b %result%
