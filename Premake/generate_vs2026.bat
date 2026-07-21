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

echo ===== Verify ABI Table =====
rem C++ ManagedNativeApiTable と C# NativeApiTable の関数ポインタ列の数と並びが一致するか検査する。
rem 手動ミラーの drift を build 前に失敗させる。
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0verify_abi_table.ps1" -NativeHeader "%ENGINE_ROOT%\Project\Engine\Core\Scripting\Managed\ManagedScriptTypes.h" -CsFile "%ENGINE_ROOT%\Project\Engine\Managed\NEM.ScriptCore\Runtime\NativeApi.cs"
if errorlevel 1 (
    echo [ERROR] ABI table verify failed.
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

echo ===== Patch Imported Game Projects =====
rem Tools/Import で取り込んだ Project/GameProjects/* を、Sandboxと同様に
rem slnx登録 / managed設定 / デバッガ設定 / nativeデバッグ設定までパッチする。
rem ゲームはProject/GameProjects 配下と1階層深いので、相対パスや作業ディレクトリを補正して渡す。
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
rem  取り込みゲーム1件分のVSプロジェクトをパッチする
rem  %1 = ゲームプロジェクトのコンテナフォルダ (Project/GameProjects 配下)
rem ============================================================================
:PatchGameProject
setlocal
rem %1 = コンテナ(Project/GameProjects 配下)。アプリ本体はその中の同名フォルダ。
set "GP_CONTAINER=%~1"
set "GP_NAME=%~nx1"
set "GP_APP=%GP_CONTAINER%\%GP_NAME%"
echo --- Game project: %GP_NAME% ---

rem GameScripts.csprojはランタイム契約を維持したまま、ゲームごとのソリューションフォルダーへ登録する。
rem Sandboxと同名でも別フォルダー配下ならVisual Studio上で共存でき、補完とコード解析が有効になる。
if exist "%GP_APP%\Scripts\GameScripts.csproj" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_script_slnx.ps1" -SlnxPath "%ENGINE_ROOT%\Project\NEMEngine.slnx" -GameScriptsProject "%GP_APP%\Scripts\GameScripts.csproj" -GameScriptsSolutionFolder "GameProjects\%GP_NAME%"
    if errorlevel 1 (
        echo [ERROR] Failed to add C# project to solution: %GP_NAME%
        endlocal
        exit /b 1
    )
)

rem 作業ディレクトリはアプリの1つ上(コンテナ)。コンテナ直下でGameAssetsを持つのはappだけなので、
rem RuntimePaths がSandboxへフォールバックせず確実にこのゲームを拾う。エンジンのProjectルートは環境変数で示す。
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_vcxproj_user_debugger.ps1" -ProjectUserPath "%GP_APP%\%GP_NAME%.vcxproj.user" -WorkingDirectory ".." -EnvironmentVariables "NEMENGINE_ROOT=%ENGINE_ROOT%\Project"
if errorlevel 1 (
    echo [ERROR] Failed to patch debugger settings: %GP_NAME%
    endlocal
    exit /b 1
)

rem アプリは Project/GameProjects 配下で2段ネスト(コンテナ/アプリ)と4階層深いので、リポジトリルートまでは ..\..\..\.. 。
rem 末尾に\を付けるとcmd→powershellの引数解析で末尾 \" がエスケープ扱いされ値が壊れるため、末尾\無しで渡す。
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
