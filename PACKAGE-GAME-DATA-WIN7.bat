@echo off
call "%~dp0tools\run-win7.cmd" --package-only %*
exit /b %errorlevel%
