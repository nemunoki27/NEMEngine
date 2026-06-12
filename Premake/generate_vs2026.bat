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
rem ManagedComponentBindings.json から C++ dispatch / C# wrapper を生成する。
rem premake が .generated.cpp を glob する前・ScriptCore ビルド前に確定させる必要がある。
dotnet run --project "%ENGINE_ROOT%\Project\Tools\NEM.ComponentBindingGen\NEM.ComponentBindingGen.csproj" -c Release -- ^
    --metadata "%ENGINE_ROOT%\Project\Engine\Core\Scripting\Managed\Bindings\ManagedComponentBindings.json" ^
    --out-native-dir "%ENGINE_ROOT%\Project\Engine\Core\Scripting\Managed\Generated" ^
    --out-cs-dir "%ENGINE_ROOT%\Project\Engine\Managed\NEM.ScriptCore\Generated"
if errorlevel 1 (
    echo [ERROR] Component binding generation failed.
    popd
    exit /b 1
)

echo ===== Verify Component Inventory =====
rem inventory(全 registered component の分類) と generated schema/出力の整合を検査する。
rem ReviewCandidate 残存・registry 欠落・schema 対応ずれ・generated drift を build 前に失敗させる。
dotnet run --project "%ENGINE_ROOT%\Project\Tools\NEM.ComponentBindingGen\NEM.ComponentBindingGen.csproj" -c Release -- ^
    --verify ^
    --metadata "%ENGINE_ROOT%\Project\Engine\Core\Scripting\Managed\Bindings\ManagedComponentBindings.json" ^
    --inventory "%ENGINE_ROOT%\Project\Engine\Core\Scripting\Managed\Bindings\ManagedComponentInventory.json" ^
    --registry "%ENGINE_ROOT%\Project\Engine\Core\Scripting\Managed\Bindings\RegisteredComponents.txt" ^
    --out-native-dir "%ENGINE_ROOT%\Project\Engine\Core\Scripting\Managed\Generated" ^
    --out-cs-dir "%ENGINE_ROOT%\Project\Engine\Managed\NEM.ScriptCore\Generated"
if errorlevel 1 (
    echo [ERROR] Component inventory verify failed.
    popd
    exit /b 1
)

echo ===== Cleanup Old Project Files =====
if exist "%ENGINE_ROOT%\Project\NEMEngine.sln" del /q "%ENGINE_ROOT%\Project\NEMEngine.sln"
if exist "%ENGINE_ROOT%\Project\Engine\NEMEngine.vcxproj" del /q "%ENGINE_ROOT%\Project\Engine\NEMEngine.vcxproj"
if exist "%ENGINE_ROOT%\Project\Engine\NEMEngine.vcxproj.filters" del /q "%ENGINE_ROOT%\Project\Engine\NEMEngine.vcxproj.filters"
if exist "%ENGINE_ROOT%\Project\Engine\NEMEngine.vcxproj.user" del /q "%ENGINE_ROOT%\Project\Engine\NEMEngine.vcxproj.user"

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
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_native_debug_settings.ps1" -ProjectPaths "%ENGINE_ROOT%\Project\Engine\NEMEngine.vcxproj"
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
popd
endlocal
exit /b 0
