@echo off
setlocal

pushd "%~dp0"

for %%I in ("%~dp0..") do set "ENGINE_ROOT=%%~fI"

echo ===== Cleanup Old Project Files =====
if exist "%ENGINE_ROOT%\Project\NEMEngine.vcxproj" del /q "%ENGINE_ROOT%\Project\NEMEngine.vcxproj"
if exist "%ENGINE_ROOT%\Project\NEMEngine.vcxproj.filters" del /q "%ENGINE_ROOT%\Project\NEMEngine.vcxproj.filters"
if exist "%ENGINE_ROOT%\Project\NEMEngine.vcxproj.user" del /q "%ENGINE_ROOT%\Project\NEMEngine.vcxproj.user"

if exist "%ENGINE_ROOT%\Project\Sandbox.vcxproj" del /q "%ENGINE_ROOT%\Project\Sandbox.vcxproj"
if exist "%ENGINE_ROOT%\Project\Sandbox.vcxproj.filters" del /q "%ENGINE_ROOT%\Project\Sandbox.vcxproj.filters"
if exist "%ENGINE_ROOT%\Project\Sandbox.vcxproj.user" del /q "%ENGINE_ROOT%\Project\Sandbox.vcxproj.user"

echo ===== Premake Version =====
premake5.exe --version
echo.

echo ===== Available Actions =====
premake5.exe --help | findstr /i "vs2026 vs2022"
echo.

echo ===== Generate Start =====
premake5.exe --file="%~dp0premake5.lua" vs2026 > premake_error.log 2>&1

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