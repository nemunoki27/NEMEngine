@echo off
setlocal

pushd "%~dp0"

echo ===== Premake Version =====
premake5.exe --version
echo.

echo ===== Available Actions =====
premake5.exe --help | findstr /i "vs2026 vs2022"
echo.

echo ===== Generate Start =====
premake5.exe vs2026 > premake_error.log 2>&1

type premake_error.log
echo.

if errorlevel 1 (
    echo [ERROR] Premake generation failed.
) else (
    echo [OK] Premake generation succeeded.
)

echo.
pause
popd

endlocal