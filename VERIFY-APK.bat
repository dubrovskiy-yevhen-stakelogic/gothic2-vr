@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\verify-apk.ps1" %*
exit /b %errorlevel%
