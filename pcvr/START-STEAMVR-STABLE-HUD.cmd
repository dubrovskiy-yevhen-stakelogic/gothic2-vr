@echo off
setlocal
cd /d "%~dp0"
set "GOTHIC2VR_STEAMVR_CROPPED_HUD=0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-PCVR.ps1" -Runtime SteamVR %*
if exist "log.txt" copy /y "log.txt" "log-steamvr-stable.txt" >nul
pause
endlocal
