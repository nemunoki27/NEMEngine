@echo off
rem Double-click to create a new game project (interactive, Japanese prompts).
chcp 65001 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0NewGameInteractive.ps1"
if errorlevel 1 pause
