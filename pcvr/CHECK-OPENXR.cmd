@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-PCVR.ps1" -Info %*
if exist vrinfo.txt type vrinfo.txt
pause
