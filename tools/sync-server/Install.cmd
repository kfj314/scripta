@echo off
rem Double-click to install the Scripta Sync Server tray app.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
echo.
pause
