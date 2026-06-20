@echo off
rem Double-click to publish the prebuilt SDK to its own Git repository.
chcp 65001 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0PublishSDK.ps1"
if errorlevel 1 pause
