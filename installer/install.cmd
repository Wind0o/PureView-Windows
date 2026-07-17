@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
if errorlevel 1 (
  echo.
  echo PureView installation failed.
  pause
  exit /b 1
)
echo.
echo PureView installation completed.
pause
