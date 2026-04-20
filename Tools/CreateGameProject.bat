@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "ENGINE_ROOT=%%~fI"
set "TEMPLATE_ROOT=%ENGINE_ROOT%\Templates\GameProject"

if "%~1"=="" (
    set /p "GAME_NAME=Create GameProject: "
) else (
    set "GAME_NAME=%~1"
)

if not defined GAME_NAME (
    echo [ERROR] Game name is empty.
    exit /b 1
)

echo.%GAME_NAME%| findstr /r "^[A-Za-z_][A-Za-z0-9_]*$" >nul
if errorlevel 1 (
    echo [ERROR] Game name must match [A-Za-z_][A-Za-z0-9_]*
    exit /b 1
)

if not exist "%TEMPLATE_ROOT%\Premake\premake5.lua" (
    echo [ERROR] Template not found: %TEMPLATE_ROOT%
    exit /b 1
)

if not exist "%ENGINE_ROOT%\Project\Sandbox" (
    echo [ERROR] Sandbox template not found: %ENGINE_ROOT%\Project\Sandbox
    exit /b 1
)

set "GAME_ROOT=%CD%\%GAME_NAME%"
if exist "%GAME_ROOT%" (
    echo [ERROR] Already exists: %GAME_ROOT%
    exit /b 1
)

set "TEMP_DIR=%TEMP%\NEMCreateGame_%RANDOM%%RANDOM%"
mkdir "%TEMP_DIR%\Project" >nul 2>&1

echo [1/7] Export Sandbox template...
git -C "%ENGINE_ROOT%" rev-parse --verify main >nul 2>&1
if not errorlevel 1 (
    git -C "%ENGINE_ROOT%" archive --format=tar main Project/Sandbox Project/Assets 2>nul | tar -xf - -C "%TEMP_DIR%"
)

if not exist "%TEMP_DIR%\Project\Sandbox" (
    echo [WARN] Could not export from branch "main". Copying current working tree instead.
    xcopy "%ENGINE_ROOT%\Project\Sandbox" "%TEMP_DIR%\Project\Sandbox\" /E /I /Y >nul
    if exist "%ENGINE_ROOT%\Project\Assets" (
        xcopy "%ENGINE_ROOT%\Project\Assets" "%TEMP_DIR%\Project\Assets\" /E /I /Y >nul
    )
)

if not exist "%TEMP_DIR%\Project\Sandbox" (
    echo [ERROR] Failed to acquire Sandbox template.
    rd /s /q "%TEMP_DIR%" >nul 2>&1
    exit /b 1
)

echo [2/7] Create game directory...
mkdir "%GAME_ROOT%" >nul
mkdir "%GAME_ROOT%\Premake" >nul
mkdir "%GAME_ROOT%\Project" >nul
mkdir "%GAME_ROOT%\Generated" >nul
mkdir "%GAME_ROOT%\External" >nul
mkdir "%GAME_ROOT%\Project\Library" >nul
mkdir "%GAME_ROOT%\Project\Log" >nul

echo [3/7] Copy Sandbox and shared Assets...
xcopy "%TEMP_DIR%\Project\Sandbox" "%GAME_ROOT%\Project\%GAME_NAME%\" /E /I /Y >nul
if exist "%TEMP_DIR%\Project\Assets" (
    xcopy "%TEMP_DIR%\Project\Assets" "%GAME_ROOT%\Project\Assets\" /E /I /Y >nul
) else (
    mkdir "%GAME_ROOT%\Project\Assets" >nul
)

echo [4/7] Copy game templates...
xcopy "%TEMPLATE_ROOT%\Premake" "%GAME_ROOT%\Premake\" /E /I /Y >nul
copy /Y "%TEMPLATE_ROOT%\.gitignore" "%GAME_ROOT%\.gitignore" >nul

echo [5/7] Replace template tokens...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$gameName = '%GAME_NAME%';" ^
  "$premakePath = [IO.Path]::GetFullPath('%GAME_ROOT%\Premake\premake5.lua');" ^
  "$content = Get-Content -LiteralPath $premakePath -Raw;" ^
  "$content = $content.Replace('__GAME_NAME__', $gameName);" ^
  "[IO.File]::WriteAllText($premakePath, $content, (New-Object System.Text.UTF8Encoding($false)));"

echo [6/7] Rename Sandbox identifiers inside copied source...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$root = [IO.Path]::GetFullPath('%GAME_ROOT%\Project\%GAME_NAME%');" ^
  "$patterns = '*.cpp','*.c','*.h','*.hpp','*.inl','*.hlsl','*.fx','*.json','*.ini','*.txt','*.md';" ^
  "Get-ChildItem -Path $root -Recurse -Include $patterns | ForEach-Object {" ^
  "  $src = Get-Content -LiteralPath $_.FullName -Raw;" ^
  "  $dst = $src -replace '\bSandbox\b', '%GAME_NAME%';" ^
  "  if ($dst -ne $src) { [IO.File]::WriteAllText($_.FullName, $dst, (New-Object System.Text.UTF8Encoding($false))) }" ^
  "}"

echo [7/7] Create local settings and git repo...
> "%GAME_ROOT%\Premake\local_settings.lua" echo NEMENGINE_ROOT = [[%ENGINE_ROOT%]]
> "%GAME_ROOT%\Premake\local_settings.bat" echo @echo off
>> "%GAME_ROOT%\Premake\local_settings.bat" echo set "NEMENGINE_ROOT=%ENGINE_ROOT%"

pushd "%GAME_ROOT%"
git init >nul 2>&1
git branch -M main >nul 2>&1
popd

echo.
echo ===== Premake Generate =====
call "%GAME_ROOT%\Premake\generate_vs2026.bat"
if errorlevel 1 (
    echo [ERROR] Failed to generate Visual Studio files.
    rd /s /q "%TEMP_DIR%" >nul 2>&1
    exit /b 1
)

echo.
echo [OK] Game project created:
echo      %GAME_ROOT%
echo.
echo [FOR SHARING WITH OTHER USERS]
echo   1. cd "%GAME_ROOT%"
echo   2. git submodule add ^<NEMEngine-Repo-URL^> External/NEMEngine
echo   3. del Premake\local_settings.lua
echo   4. del Premake\local_settings.bat
echo.
echo [OPTIONAL PRIVATE GITHUB REPO]
echo   gh repo create %GAME_NAME% --private --source "%GAME_ROOT%" --remote origin
echo.

rd /s /q "%TEMP_DIR%" >nul 2>&1
endlocal