@echo off
rem Repair an existing game project from this NEMEngine checkout.
chcp 65001 >nul
setlocal

set "GAME_ROOT=%~1"
if "%GAME_ROOT%"=="" (
    set /p "GAME_ROOT=修復するゲームプロジェクトのルート: "
)

if "%GAME_ROOT%"=="" (
    echo [ERROR] Game project root is empty.
    pause
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0RepairGameProject.ps1" -GameRoot "%GAME_ROOT%"
if errorlevel 1 pause
