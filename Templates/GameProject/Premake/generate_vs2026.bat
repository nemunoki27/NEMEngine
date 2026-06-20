@echo off
setlocal

rem GameProject generate: reference the prebuilt NEMEngine SDK only and generate the game VS project.
rem No engine full build and no CMake external build are triggered.

pushd "%~dp0"

if exist "%~dp0local_settings.bat" (
    call "%~dp0local_settings.bat"
)

for %%I in ("%~dp0..") do set "GAME_ROOT=%%~fI"
for %%I in ("%GAME_ROOT%") do set "GAME_NAME=%%~nxI"

rem Resolve SDK root: NEM_SDK_ROOT(local_settings) or External\NEMEngine
if not defined NEM_SDK_ROOT if exist "%GAME_ROOT%\External\NEMEngine\Include\NEMEngineRuntime.h" set "NEM_SDK_ROOT=%GAME_ROOT%\External\NEMEngine"

if not defined NEM_SDK_ROOT (
    echo [ERROR] NEMEngine SDK is not found.
    echo         Place the prebuilt SDK at External\NEMEngine, or set NEM_SDK_ROOT in Premake\local_settings.bat
    popd
    exit /b 1
)

for %%I in ("%NEM_SDK_ROOT%") do set "NEM_SDK_ROOT=%%~fI"

if not exist "%NEM_SDK_ROOT%\Premake\premake5.exe" (
    echo [ERROR] premake5.exe was not found in SDK: %NEM_SDK_ROOT%\Premake\premake5.exe
    popd
    exit /b 1
)

echo ===== Cleanup Old Project Files =====
if exist "%GAME_ROOT%\Project\%GAME_NAME%.slnx" del /q "%GAME_ROOT%\Project\%GAME_NAME%.slnx"
if exist "%GAME_ROOT%\Project\%GAME_NAME%\%GAME_NAME%.vcxproj" del /q "%GAME_ROOT%\Project\%GAME_NAME%\%GAME_NAME%.vcxproj"
if exist "%GAME_ROOT%\Project\%GAME_NAME%\%GAME_NAME%.vcxproj.filters" del /q "%GAME_ROOT%\Project\%GAME_NAME%\%GAME_NAME%.vcxproj.filters"

echo ===== Generate Start =====
"%NEM_SDK_ROOT%\Premake\premake5.exe" --file="%~dp0premake5.lua" --engine-root="%NEM_SDK_ROOT%" vs2026 > "%~dp0premake_error.log" 2>&1
type "%~dp0premake_error.log"
echo.

if errorlevel 1 (
    echo [ERROR] Premake generation failed.
    popd
    exit /b 1
)

echo [OK] Premake generation succeeded.

rem Add the C# script project to the solution and set the debugger working directory.
if exist "%NEM_SDK_ROOT%\Premake\patch_script_slnx.ps1" powershell -NoProfile -ExecutionPolicy Bypass -File "%NEM_SDK_ROOT%\Premake\patch_script_slnx.ps1" -SlnxPath "%GAME_ROOT%\Project\%GAME_NAME%.slnx" -GameScriptsProject "%GAME_ROOT%\Project\%GAME_NAME%\Scripts\GameScripts.csproj"
if exist "%NEM_SDK_ROOT%\Premake\patch_vcxproj_user_debugger.ps1" powershell -NoProfile -ExecutionPolicy Bypass -File "%NEM_SDK_ROOT%\Premake\patch_vcxproj_user_debugger.ps1" -ProjectUserPath "%GAME_ROOT%\Project\%GAME_NAME%\%GAME_NAME%.vcxproj.user" -WorkingDirectory ".."

popd
endlocal
exit /b 0
