@echo off
setlocal

pushd "%~dp0"

if exist "%~dp0local_settings.bat" (
    call "%~dp0local_settings.bat"
)

for %%I in ("%~dp0..") do set "GAME_ROOT=%%~fI"
for %%I in ("%GAME_ROOT%") do set "GAME_NAME=%%~nxI"

if not defined NEMENGINE_ROOT if exist "%GAME_ROOT%\External\NEMEngine\Premake\premake5.exe" (
    set "NEMENGINE_ROOT=%GAME_ROOT%\External\NEMEngine"
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

call "%NEMENGINE_ROOT%\Premake\generate_vs2026.bat"
if errorlevel 1 (
    popd
    exit /b 1
)

echo ===== Cleanup Old Project Files =====
if exist "%GAME_ROOT%\Project\Engine\NEMEngine.vcxproj" del /q "%GAME_ROOT%\Project\Engine\NEMEngine.vcxproj"
if exist "%GAME_ROOT%\Project\Engine\NEMEngine.vcxproj.filters" del /q "%GAME_ROOT%\Project\Engine\NEMEngine.vcxproj.filters"
if exist "%GAME_ROOT%\Project\Engine\NEMEngine.vcxproj.user" del /q "%GAME_ROOT%\Project\Engine\NEMEngine.vcxproj.user"

if exist "%GAME_ROOT%\Project\Externals\imgui\imgui.vcxproj" del /q "%GAME_ROOT%\Project\Externals\imgui\imgui.vcxproj"
if exist "%GAME_ROOT%\Project\Externals\imgui\imgui.vcxproj.filters" del /q "%GAME_ROOT%\Project\Externals\imgui\imgui.vcxproj.filters"
if exist "%GAME_ROOT%\Project\Externals\imgui\imgui.vcxproj.user" del /q "%GAME_ROOT%\Project\Externals\imgui\imgui.vcxproj.user"

if exist "%GAME_ROOT%\Project\%GAME_NAME%.vcxproj" del /q "%GAME_ROOT%\Project\%GAME_NAME%.vcxproj"
if exist "%GAME_ROOT%\Project\%GAME_NAME%.vcxproj.filters" del /q "%GAME_ROOT%\Project\%GAME_NAME%.vcxproj.filters"
if exist "%GAME_ROOT%\Project\%GAME_NAME%.vcxproj.user" del /q "%GAME_ROOT%\Project\%GAME_NAME%.vcxproj.user"

if exist "%GAME_ROOT%\Project\%GAME_NAME%\Sandbox.vcxproj" del /q "%GAME_ROOT%\Project\%GAME_NAME%\Sandbox.vcxproj"
if exist "%GAME_ROOT%\Project\%GAME_NAME%\Sandbox.vcxproj.filters" del /q "%GAME_ROOT%\Project\%GAME_NAME%\Sandbox.vcxproj.filters"
if exist "%GAME_ROOT%\Project\%GAME_NAME%\Sandbox.vcxproj.user" del /q "%GAME_ROOT%\Project\%GAME_NAME%\Sandbox.vcxproj.user"

if exist "%GAME_ROOT%\Project\%GAME_NAME%\Assets" (
    2>nul rd "%GAME_ROOT%\Project\%GAME_NAME%\Assets"
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

powershell -NoProfile -ExecutionPolicy Bypass -File "%NEMENGINE_ROOT%\Premake\patch_script_slnx.ps1" -SlnxPath "%GAME_ROOT%\Project\%GAME_NAME%.slnx" -ScriptCoreProject "%NEMENGINE_ROOT%\Project\Engine\Managed\NEM.ScriptCore\NEM.ScriptCore.csproj" -GameScriptsProject "%GAME_ROOT%\Project\%GAME_NAME%\Scripts\GameScripts.csproj"
if errorlevel 1 (
    echo [ERROR] Failed to patch C# projects into the solution.
    popd
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%NEMENGINE_ROOT%\Premake\patch_vcxproj_user_debugger.ps1" -ProjectUserPath "%GAME_ROOT%\Project\%GAME_NAME%\%GAME_NAME%.vcxproj.user" -WorkingDirectory ".."
if errorlevel 1 (
    echo [ERROR] Failed to patch game debugger settings.
    popd
    exit /b 1
)

popd
endlocal
exit /b 0
