@echo off
setlocal

pushd "%~dp0"

for %%I in ("%~dp0..") do set "ENGINE_ROOT=%%~fI"

call "%~dp0configure_externals_vs2026.bat"
if errorlevel 1 (
    popd
    exit /b 1
)

echo ===== Generate Component Bindings =====
rem Generate registration, bindings, and ABI from the component manifests.
dotnet run --project "%ENGINE_ROOT%\Project\Tools\NEM.ComponentBindingGen\NEM.ComponentBindingGen.csproj" -c Release -- ^
    --manifest "%ENGINE_ROOT%\Project\Engine\Core\Scripting\Managed\Bindings\ComponentManifest.json" ^
    --abi "%ENGINE_ROOT%\Project\Engine\Core\Scripting\Managed\Bindings\ManagedNativeApi.json" ^
    --out-native-dir "%ENGINE_ROOT%\Project\Engine\Core\Scripting\Managed\Generated" ^
    --out-cs-dir "%ENGINE_ROOT%\Project\Engine\Managed\NEM.ScriptCore\Generated"
if errorlevel 1 (
    echo [ERROR] Component binding generation failed.
    popd
    exit /b 1
)

echo ===== Verify Generated Bindings =====
dotnet run --project "%ENGINE_ROOT%\Project\Tools\NEM.ComponentBindingGen\NEM.ComponentBindingGen.csproj" -c Release -- ^
    --verify ^
    --manifest "%ENGINE_ROOT%\Project\Engine\Core\Scripting\Managed\Bindings\ComponentManifest.json" ^
    --abi "%ENGINE_ROOT%\Project\Engine\Core\Scripting\Managed\Bindings\ManagedNativeApi.json" ^
    --out-native-dir "%ENGINE_ROOT%\Project\Engine\Core\Scripting\Managed\Generated" ^
    --out-cs-dir "%ENGINE_ROOT%\Project\Engine\Managed\NEM.ScriptCore\Generated"
if errorlevel 1 (
    echo [ERROR] Generated binding verify failed.
    popd
    exit /b 1
)

echo ===== Cleanup Old Project Files =====
if exist "%ENGINE_ROOT%\Project\NEMEngine.sln" del /q "%ENGINE_ROOT%\Project\NEMEngine.sln"
if exist "%ENGINE_ROOT%\Project\NEMEngine.slnx" del /q "%ENGINE_ROOT%\Project\NEMEngine.slnx"
if exist "%ENGINE_ROOT%\Project\Engine\NEMEngine.vcxproj" del /q "%ENGINE_ROOT%\Project\Engine\NEMEngine.vcxproj"
if exist "%ENGINE_ROOT%\Project\Engine\NEMEngine.vcxproj.filters" del /q "%ENGINE_ROOT%\Project\Engine\NEMEngine.vcxproj.filters"
if exist "%ENGINE_ROOT%\Project\Engine\NEMEngine.vcxproj.user" del /q "%ENGINE_ROOT%\Project\Engine\NEMEngine.vcxproj.user"
if exist "%ENGINE_ROOT%\Project\Engine\NEMRuntime.vcxproj" del /q "%ENGINE_ROOT%\Project\Engine\NEMRuntime.vcxproj"
if exist "%ENGINE_ROOT%\Project\Engine\NEMRuntime.vcxproj.filters" del /q "%ENGINE_ROOT%\Project\Engine\NEMRuntime.vcxproj.filters"
if exist "%ENGINE_ROOT%\Project\Engine\NEMRuntime.vcxproj.user" del /q "%ENGINE_ROOT%\Project\Engine\NEMRuntime.vcxproj.user"
if exist "%ENGINE_ROOT%\Project\Engine\Editor\NEMEditor.vcxproj" del /q "%ENGINE_ROOT%\Project\Engine\Editor\NEMEditor.vcxproj"
if exist "%ENGINE_ROOT%\Project\Engine\Editor\NEMEditor.vcxproj.filters" del /q "%ENGINE_ROOT%\Project\Engine\Editor\NEMEditor.vcxproj.filters"
if exist "%ENGINE_ROOT%\Project\Engine\Editor\NEMEditor.vcxproj.user" del /q "%ENGINE_ROOT%\Project\Engine\Editor\NEMEditor.vcxproj.user"

if exist "%ENGINE_ROOT%\Project\Sandbox\Sandbox.vcxproj" del /q "%ENGINE_ROOT%\Project\Sandbox\Sandbox.vcxproj"
if exist "%ENGINE_ROOT%\Project\Sandbox\Sandbox.vcxproj.filters" del /q "%ENGINE_ROOT%\Project\Sandbox\Sandbox.vcxproj.filters"
if exist "%ENGINE_ROOT%\Project\Sandbox\Sandbox.vcxproj.user" del /q "%ENGINE_ROOT%\Project\Sandbox\Sandbox.vcxproj.user"

echo ===== Generate Start =====
"%~dp0premake5.exe" --file="%~dp0premake5.lua" vs2026 > "%~dp0premake_error.log" 2>&1
set "PREMAKE_RC=%ERRORLEVEL%"

type "%~dp0premake_error.log"
echo.

findstr /c:"Error:" "%~dp0premake_error.log" >nul
set "FINDSTR_RC=%ERRORLEVEL%"

if not "%PREMAKE_RC%"=="0" (
    echo [ERROR] Premake generation failed. ^(premake exit code=%PREMAKE_RC%^)
    popd
    exit /b 1
)

if "%FINDSTR_RC%"=="0" (
    echo [ERROR] Premake generation failed. ^(Error: found in premake_error.log^)
    popd
    exit /b 1
)

del /q "%~dp0premake_error.log" >nul 2>&1
echo [OK] Premake generation succeeded.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_script_slnx.ps1" -SlnxPath "%ENGINE_ROOT%\Project\NEMEngine.slnx" -ScriptCoreProject "%ENGINE_ROOT%\Project\Engine\Managed\NEM.ScriptCore\NEM.ScriptCore.csproj" -GameScriptsProject "%ENGINE_ROOT%\Project\Sandbox\Scripts\GameScripts.csproj"
if errorlevel 1 (
    echo [ERROR] Failed to patch C# projects into the solution.
    popd
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_vcxproj_user_debugger.ps1" -ProjectUserPath "%ENGINE_ROOT%\Project\Sandbox\Sandbox.vcxproj.user" -WorkingDirectory ".."
if errorlevel 1 (
    echo [ERROR] Failed to patch Sandbox debugger settings.
    popd
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_vcxproj_managed_config.ps1" -ProjectPath "%ENGINE_ROOT%\Project\Sandbox\Sandbox.vcxproj" -ScriptCoreProjectPath "%ENGINE_ROOT%\Project\Engine\Managed\NEM.ScriptCore\NEM.ScriptCore.csproj" -ScriptCoreManagedOutputPath "%ENGINE_ROOT%\Generated\Managed\NEM.ScriptCore"
if errorlevel 1 (
    echo [ERROR] Failed to patch Sandbox managed build/copy settings.
    popd
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_native_debug_settings.ps1" -ProjectPaths "%ENGINE_ROOT%\Project\Engine\NEMRuntime.vcxproj"
if errorlevel 1 (
    echo [ERROR] Failed to patch native debug settings.
    popd
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_native_debug_settings.ps1" -ProjectPaths "%ENGINE_ROOT%\Project\Engine\Editor\NEMEditor.vcxproj"
if errorlevel 1 (
    echo [ERROR] Failed to patch native debug settings.
    popd
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_native_debug_settings.ps1" -ProjectPaths "%ENGINE_ROOT%\Project\Sandbox\Sandbox.vcxproj"
if errorlevel 1 (
    echo [ERROR] Failed to patch native debug settings.
    popd
    exit /b 1
)

echo ===== Patch Imported Game Projects =====
rem Patch imported game projects and their managed/debug settings.
if exist "%ENGINE_ROOT%\Project\GameProjects" (
    for /d %%G in ("%ENGINE_ROOT%\Project\GameProjects\*") do (
        if exist "%%G\%%~nxG\GameAssets" (
            call :PatchGameProject "%%G"
            if errorlevel 1 (
                popd
                exit /b 1
            )
        )
    )
)

popd
endlocal
exit /b 0

rem ============================================================================
rem Patch one imported game project.
rem %1 = game project container under Project/GameProjects.
rem ============================================================================
:PatchGameProject
setlocal
set "GP_CONTAINER=%~1"
set "GP_NAME=%~nx1"
set "GP_APP=%GP_CONTAINER%\%GP_NAME%"
echo --- Game project: %GP_NAME% ---

rem Add GameScripts.csproj to the game-specific solution folder.
if exist "%GP_APP%\Scripts\GameScripts.csproj" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_script_slnx.ps1" -SlnxPath "%ENGINE_ROOT%\Project\NEMEngine.slnx" -GameScriptsProject "%GP_APP%\Scripts\GameScripts.csproj" -GameScriptsSolutionFolder "GameProjects\%GP_NAME%"
    if errorlevel 1 (
        echo [ERROR] Failed to add C# project to solution: %GP_NAME%
        endlocal
        exit /b 1
    )
)

rem Keep the working directory at the game project container.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_vcxproj_user_debugger.ps1" -ProjectUserPath "%GP_APP%\%GP_NAME%.vcxproj.user" -WorkingDirectory ".." -EnvironmentVariables "NEMENGINE_ROOT=%ENGINE_ROOT%\Project"
if errorlevel 1 (
    echo [ERROR] Failed to patch debugger settings: %GP_NAME%
    endlocal
    exit /b 1
)

rem The app is four levels below the repository root.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_vcxproj_managed_config.ps1" -ProjectPath "%GP_APP%\%GP_NAME%.vcxproj" -ScriptCoreProjectPath "%ENGINE_ROOT%\Project\Engine\Managed\NEM.ScriptCore\NEM.ScriptCore.csproj" -ScriptCoreManagedOutputPath "%ENGINE_ROOT%\Generated\Managed\NEM.ScriptCore" -RepoRootFromProject "..\..\..\.."
if errorlevel 1 (
    echo [ERROR] Failed to patch managed build/copy settings: %GP_NAME%
    endlocal
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_native_debug_settings.ps1" -ProjectPaths "%GP_APP%\%GP_NAME%.vcxproj"
if errorlevel 1 (
    echo [ERROR] Failed to patch native debug settings: %GP_NAME%
    endlocal
    exit /b 1
)

endlocal
exit /b 0
