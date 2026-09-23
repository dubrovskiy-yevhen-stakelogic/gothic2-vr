@echo off
setlocal
cd /d "%~dp0"
set "GOTHIC2VR_NO_MIRROR_TEST=1"
set "GOTHIC2VR_STEAMVR_CROPPED_HUD=1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-PCVR.ps1" -Runtime SteamVR %*
if exist "log.txt" copy /y "log.txt" "log-steamvr-no-mirror.txt" >nul
pause
endlocal
