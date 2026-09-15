@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install-quest.ps1" %*
exit /b %errorlevel%
