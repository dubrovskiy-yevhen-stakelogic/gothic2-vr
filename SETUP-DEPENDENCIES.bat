@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup-dependencies.ps1" %*
set "result=%errorlevel%"
if not "%result%"=="0" echo Failed. Read the error above.
pause
exit /b %result%
