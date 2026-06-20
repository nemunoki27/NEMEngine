@echo off
rem Double-click to build the engine and export the prebuilt SDK (Debug/Develop/Release).
chcp 65001 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0PackageEngineSDK.ps1"
echo.
pause
