@echo off
call "%~dp0tools\run-win7.cmd" --update-only %*
exit /b %errorlevel%
