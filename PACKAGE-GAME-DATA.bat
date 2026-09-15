@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\package-game-data.ps1" %*
exit /b %errorlevel%
