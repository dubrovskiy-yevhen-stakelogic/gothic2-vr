@echo off
setlocal
set "PYTHON=%~dp0..\legacy\python\python.exe"
if not exist "%PYTHON%" (
  echo Extract the complete Windows 7 compatibility player package first.
  pause
  exit /b 1
)
"%PYTHON%" -c "import sys; sys.exit(0 if sys.version_info[:2] == (3,8) else 1)"
if errorlevel 1 (
  echo Python could not start. This package needs Windows 7 SP1 and Universal C Runtime KB2999226.
  echo Keep the exact Windows DLL error message for the support report.
  pause
  exit /b 1
)
"%PYTHON%" -B "%~dp0install-win7.py" %*
set "RESULT=%errorlevel%"
pause
exit /b %RESULT%
