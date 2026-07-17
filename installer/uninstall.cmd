@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1" %*
if errorlevel 1 (
  echo.
  echo PureView uninstall failed.
  pause
  exit /b 1
)
