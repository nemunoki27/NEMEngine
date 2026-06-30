@echo off
setlocal

rem ============================================================================
rem  ImportGameProject.bat
rem  NEMEngineをサブモジュール参照しているゲームプロジェクトを取り込み、
rem  Project/GameProjects 配下へ複製してスタートアッププロジェクトとして起動できる状態にする。
rem
rem  使い方:
rem    ImportGameProject.bat                  ... 実行後にURLを入力
rem    ImportGameProject.bat  GitのURL         ... URLを引数で渡す
rem ============================================================================

set "URL=%~1"
if "%URL%"=="" (
    set /p "URL=取り込むゲームプロジェクトのGit URLを入力してください: "
)

if "%URL%"=="" (
    echo [ERROR] URLが指定されていません。
    pause
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Import-GameProject.ps1" -GitUrl "%URL%"
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" (
    echo [ERROR] 取り込みに失敗しました。
    pause
    exit /b %RC%
)

echo [OK] 取り込みが完了しました。
pause
endlocal
