@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\open-data-import.ps1" %*
exit /b %errorlevel%
