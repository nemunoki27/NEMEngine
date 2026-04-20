@echo off
setlocal

pushd "%~dp0"

if exist "%~dp0local_settings.bat" (
    call "%~dp0local_settings.bat"
)

if not defined NEMENGINE_ROOT if exist "%~dp0..\External\NEMEngine\Premake\premake5.exe" (
    set "NEMENGINE_ROOT=%~dp0..\External\NEMEngine"
)

if not defined NEMENGINE_ROOT (
    echo [ERROR] NEMENGINE_ROOT is not set.
    echo         Create Premake\local_settings.bat or place the engine in External\NEMEngine.
    popd
    exit /b 1
)

for %%I in ("%NEMENGINE_ROOT%") do set "NEMENGINE_ROOT=%%~fI"

if not exist "%NEMENGINE_ROOT%\Premake\premake5.exe" (
    echo [ERROR] premake5.exe was not found:
    echo         %NEMENGINE_ROOT%\Premake\premake5.exe
    popd
    exit /b 1
)

echo ===== Generate Start =====
"%NEMENGINE_ROOT%\Premake\premake5.exe" --file="%~dp0premake5.lua" vs2026 > premake_error.log 2>&1
type premake_error.log
echo.

if errorlevel 1 (
    echo [ERROR] Premake generation failed.
    popd
    exit /b 1
) else (
    echo [OK] Premake generation succeeded.
)

popd
endlocal