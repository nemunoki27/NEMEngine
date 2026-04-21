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
set "TEMP_SANDBOX=%TEMP_DIR%\Project\Sandbox"
mkdir "%TEMP_DIR%\Project" >nul 2>&1

echo [1/10] Export Sandbox template from engine...
robocopy "%ENGINE_ROOT%\Project\Sandbox" "%TEMP_SANDBOX%" /E /NFL /NDL /NJH /NJS /NP ^
    /XD ".vs" "Generated" "Library" "Log" ^
    /XF "*.sln" "*.slnx" "*.vcxproj" "*.vcxproj.filters" "*.vcxproj.user" "*.db" "*.opendb" "*.sdf" "*.suo"
set "RC=!ERRORLEVEL!"
if !RC! GEQ 8 (
    echo [ERROR] Failed to copy Sandbox template. robocopy errorlevel=!RC!
    rd /s /q "%TEMP_DIR%" >nul 2>&1
    exit /b 1
)

if not exist "%TEMP_SANDBOX%" (
    echo [ERROR] Failed to acquire Sandbox template.
    rd /s /q "%TEMP_DIR%" >nul 2>&1
    exit /b 1
)

echo [2/10] Create game directory...
mkdir "%GAME_ROOT%" >nul
mkdir "%GAME_ROOT%\Premake" >nul
mkdir "%GAME_ROOT%\Project" >nul
mkdir "%GAME_ROOT%\Project\%GAME_NAME%" >nul

if exist "%ENGINE_ROOT%\Project\EditorLayout.ini" (
    copy /Y "%ENGINE_ROOT%\Project\EditorLayout.ini" "%GAME_ROOT%\Project\EditorLayout.ini" >nul
)

echo [3/10] Copy sanitized Sandbox to Project\%GAME_NAME%...
robocopy "%TEMP_SANDBOX%" "%GAME_ROOT%\Project\%GAME_NAME%" /E /NFL /NDL /NJH /NJS /NP ^
    /XD ".vs" "Generated" "Library" "Log" ^
    /XF "*.sln" "*.slnx" "*.vcxproj" "*.vcxproj.filters" "*.vcxproj.user" "*.db" "*.opendb" "*.sdf" "*.suo"
set "RC=!ERRORLEVEL!"
if !RC! GEQ 8 (
    echo [ERROR] Failed to copy sanitized Sandbox. robocopy errorlevel=!RC!
    rd /s /q "%TEMP_DIR%" >nul 2>&1
    exit /b 1
)

echo [4/10] Copy game templates...
robocopy "%TEMPLATE_ROOT%\Premake" "%GAME_ROOT%\Premake" /E /NFL /NDL /NJH /NJS /NP >nul
set "RC=!ERRORLEVEL!"
if !RC! GEQ 8 (
    echo [ERROR] Failed to copy Premake templates. robocopy errorlevel=!RC!
    rd /s /q "%TEMP_DIR%" >nul 2>&1
    exit /b 1
)

if exist "%TEMPLATE_ROOT%\.gitignore" (
    copy /Y "%TEMPLATE_ROOT%\.gitignore" "%GAME_ROOT%\.gitignore" >nul
)

echo [5/10] Replace template tokens...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$gameName = '%GAME_NAME%';" ^
  "$premakePath = [IO.Path]::GetFullPath('%GAME_ROOT%\Premake\premake5.lua');" ^
  "$content = Get-Content -LiteralPath $premakePath -Raw;" ^
  "$content = $content.Replace('__GAME_NAME__', $gameName);" ^
  "[IO.File]::WriteAllText($premakePath, $content, (New-Object System.Text.UTF8Encoding($false)));"

echo [6/10] Remove copied junk and legacy empty folders...
if exist "%GAME_ROOT%\Project\%GAME_NAME%\Sandbox.vcxproj" del /q "%GAME_ROOT%\Project\%GAME_NAME%\Sandbox.vcxproj"
if exist "%GAME_ROOT%\Project\%GAME_NAME%\Sandbox.vcxproj.filters" del /q "%GAME_ROOT%\Project\%GAME_NAME%\Sandbox.vcxproj.filters"
if exist "%GAME_ROOT%\Project\%GAME_NAME%\Sandbox.vcxproj.user" del /q "%GAME_ROOT%\Project\%GAME_NAME%\Sandbox.vcxproj.user"
if exist "%GAME_ROOT%\Project\%GAME_NAME%\%GAME_NAME%.vcxproj" del /q "%GAME_ROOT%\Project\%GAME_NAME%\%GAME_NAME%.vcxproj"
if exist "%GAME_ROOT%\Project\%GAME_NAME%\%GAME_NAME%.vcxproj.filters" del /q "%GAME_ROOT%\Project\%GAME_NAME%\%GAME_NAME%.vcxproj.filters"
if exist "%GAME_ROOT%\Project\%GAME_NAME%\%GAME_NAME%.vcxproj.user" del /q "%GAME_ROOT%\Project\%GAME_NAME%\%GAME_NAME%.vcxproj.user"
if exist "%GAME_ROOT%\Project\%GAME_NAME%\*.sln" del /q "%GAME_ROOT%\Project\%GAME_NAME%\*.sln"
if exist "%GAME_ROOT%\Project\%GAME_NAME%\*.slnx" del /q "%GAME_ROOT%\Project\%GAME_NAME%\*.slnx"

if exist "%GAME_ROOT%\Project\%GAME_NAME%\Assets" (
    2>nul rd "%GAME_ROOT%\Project\%GAME_NAME%\Assets"
)

if exist "%GAME_ROOT%\External" (
    2>nul rd "%GAME_ROOT%\External"
)
if exist "%GAME_ROOT%\Generated" (
    2>nul rd "%GAME_ROOT%\Generated"
)
if exist "%GAME_ROOT%\Project\Library" (
    2>nul rd "%GAME_ROOT%\Project\Library"
)
if exist "%GAME_ROOT%\Project\Log" (
    2>nul rd "%GAME_ROOT%\Project\Log"
)

echo [7/10] Rename Sandbox identifiers inside copied source files...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$root = [IO.Path]::GetFullPath('%GAME_ROOT%\Project\%GAME_NAME%');" ^
  "$patterns = '*.cpp','*.c','*.h','*.hpp','*.inl','*.hlsl','*.fx','*.json','*.ini','*.txt','*.md';" ^
  "Get-ChildItem -Path $root -Recurse -Include $patterns | ForEach-Object {" ^
  "  $src = Get-Content -LiteralPath $_.FullName -Raw;" ^
  "  $dst = $src -replace '\bSandbox\b', '%GAME_NAME%';" ^
  "  if ($dst -ne $src) { [IO.File]::WriteAllText($_.FullName, $dst, (New-Object System.Text.UTF8Encoding($false))) }" ^
  "}"

echo [8/10] Create local engine settings...
> "%GAME_ROOT%\Premake\local_settings.lua" echo NEMENGINE_ROOT = [[%ENGINE_ROOT%]]
> "%GAME_ROOT%\Premake\local_settings.bat" echo @echo off
>> "%GAME_ROOT%\Premake\local_settings.bat" echo set "NEMENGINE_ROOT=%ENGINE_ROOT%"

echo [9/11] Optional local git repository creation...
set "INIT_GIT="
set /p "INIT_GIT=Initialize local git repository? [y/N]: "

if /I not "!INIT_GIT!"=="y" if /I not "!INIT_GIT!"=="yes" goto :premake_generate

echo [Git] Initialize local git repository...
pushd "%GAME_ROOT%"
git init >nul 2>&1
git branch -M main >nul 2>&1
popd

echo [10/11] Configure git identity for this game repository...

set "DEFAULT_GIT_NAME="
set "DEFAULT_GIT_EMAIL="
set "SELECTED_GIT_NAME="
set "SELECTED_GIT_EMAIL="
set "FINAL_GIT_NAME="
set "FINAL_GIT_EMAIL="

for /f "delims=" %%A in ('git -C "%GAME_ROOT%" config --get user.name 2^>nul') do set "DEFAULT_GIT_NAME=%%A"
if not defined DEFAULT_GIT_NAME (
    for /f "delims=" %%A in ('git config --global --get user.name 2^>nul') do set "DEFAULT_GIT_NAME=%%A"
)

for /f "delims=" %%A in ('git -C "%GAME_ROOT%" config --get user.email 2^>nul') do set "DEFAULT_GIT_EMAIL=%%A"
if not defined DEFAULT_GIT_EMAIL (
    for /f "delims=" %%A in ('git config --global --get user.email 2^>nul') do set "DEFAULT_GIT_EMAIL=%%A"
)

if defined DEFAULT_GIT_NAME (
    set /p "SELECTED_GIT_NAME=Git commit user.name [!DEFAULT_GIT_NAME!]: "
    if not defined SELECTED_GIT_NAME set "SELECTED_GIT_NAME=!DEFAULT_GIT_NAME!"
) else (
    set /p "SELECTED_GIT_NAME=Git commit user.name: "
)

if defined DEFAULT_GIT_EMAIL (
    set /p "SELECTED_GIT_EMAIL=Git commit user.email [!DEFAULT_GIT_EMAIL!]: "
    if not defined SELECTED_GIT_EMAIL set "SELECTED_GIT_EMAIL=!DEFAULT_GIT_EMAIL!"
) else (
    set /p "SELECTED_GIT_EMAIL=Git commit user.email: "
)

if defined SELECTED_GIT_NAME (
    git -C "%GAME_ROOT%" config user.name "!SELECTED_GIT_NAME!" >nul 2>&1
)

if defined SELECTED_GIT_EMAIL (
    git -C "%GAME_ROOT%" config user.email "!SELECTED_GIT_EMAIL!" >nul 2>&1
)

for /f "delims=" %%A in ('git -C "%GAME_ROOT%" config --get user.name 2^>nul') do set "FINAL_GIT_NAME=%%A"
for /f "delims=" %%A in ('git -C "%GAME_ROOT%" config --get user.email 2^>nul') do set "FINAL_GIT_EMAIL=%%A"

echo [Git] Repository identity:
if defined FINAL_GIT_NAME (
    echo   user.name  = !FINAL_GIT_NAME!
) else (
    echo   user.name  = ^<not set^>
)
if defined FINAL_GIT_EMAIL (
    echo   user.email = !FINAL_GIT_EMAIL!
) else (
    echo   user.email = ^<not set^>
)

:premake_generate
echo.
echo ===== Premake Generate =====
call "%GAME_ROOT%\Premake\generate_vs2026.bat"
if errorlevel 1 (
    echo [ERROR] Failed to generate Visual Studio files.
    rd /s /q "%TEMP_DIR%" >nul 2>&1
    exit /b 1
)

if /I not "!INIT_GIT!"=="y" if /I not "!INIT_GIT!"=="yes" goto :finish

echo [10/10] Optional GitHub repository creation...
set "CREATE_GITHUB="
set /p "CREATE_GITHUB=Create GitHub repository too? [y/N]: "

if /I "!CREATE_GITHUB!"=="y" goto :create_github
if /I "!CREATE_GITHUB!"=="yes" goto :create_github
goto :finish

:create_github
where gh >nul 2>&1
if errorlevel 1 (
    echo [WARN] GitHub CLI ^(gh^) was not found.
    echo        Install GitHub CLI first, then run this manually:
    echo        cd /d "%GAME_ROOT%"
    echo        gh auth login
    echo        gh repo create %GAME_NAME% --private --source . --remote origin --push
    goto :finish
)

gh auth status >nul 2>&1
if errorlevel 1 (
    echo [WARN] GitHub CLI is not authenticated.
    echo        Run this first:
    echo        gh auth login
    echo        gh auth setup-git
    echo        Then create the remote later from:
    echo        cd /d "%GAME_ROOT%"
    echo        gh repo create %GAME_NAME% --private --source . --remote origin --push
    goto :finish
)

set "GITHUB_REPO_NAME="
set /p "GITHUB_REPO_NAME=Repository name [%GAME_NAME%]: "
if not defined GITHUB_REPO_NAME set "GITHUB_REPO_NAME=%GAME_NAME%"

set "GITHUB_VISIBILITY="
set /p "GITHUB_VISIBILITY=Repository visibility private/public [private]: "
if /I "!GITHUB_VISIBILITY!"=="public" (
    set "GITHUB_VISIBILITY=public"
) else (
    set "GITHUB_VISIBILITY=private"
)

set "GITHUB_OWNER="
set /p "GITHUB_OWNER=Repository owner or organization [leave blank for current account]: "

if /I "!GITHUB_OWNER!"=="current account" set "GITHUB_OWNER="
if /I "!GITHUB_OWNER!"=="current-account" set "GITHUB_OWNER="
if /I "!GITHUB_OWNER!"=="current" set "GITHUB_OWNER="
if /I "!GITHUB_OWNER!"=="none" set "GITHUB_OWNER="

if defined GITHUB_OWNER (
    set "GITHUB_FULL_NAME=!GITHUB_OWNER!/!GITHUB_REPO_NAME!"
) else (
    set "GITHUB_FULL_NAME=!GITHUB_REPO_NAME!"
)

echo [GitHub] Preparing initial commit...

set "FINAL_GIT_NAME="
set "FINAL_GIT_EMAIL="

for /f "delims=" %%A in ('git -C "%GAME_ROOT%" config --get user.name 2^>nul') do set "FINAL_GIT_NAME=%%A"
for /f "delims=" %%A in ('git -C "%GAME_ROOT%" config --get user.email 2^>nul') do set "FINAL_GIT_EMAIL=%%A"

if not defined FINAL_GIT_NAME (
    echo [WARN] Git user.name is not set for this repository.
    echo        Set it first with:
    echo        git -C "%GAME_ROOT%" config user.name "your_name"
    goto :finish
)

if not defined FINAL_GIT_EMAIL (
    echo [WARN] Git user.email is not set for this repository.
    echo        Set it first with:
    echo        git -C "%GAME_ROOT%" config user.email "your_email"
    goto :finish
)

git -C "%GAME_ROOT%" add . >nul 2>&1
git -C "%GAME_ROOT%" commit -m "Initial commit" >nul 2>&1

if errorlevel 1 (
    echo [WARN] Initial commit could not be created automatically.
    echo        Check git identity, then run:
    echo        cd /d "%GAME_ROOT%"
    echo        git add .
    echo        git commit -m "Initial commit"
    echo        gh repo create !GITHUB_FULL_NAME! --!GITHUB_VISIBILITY! --source . --remote origin --push
    goto :finish
)

echo [GitHub] Creating remote repository and pushing...
pushd "%GAME_ROOT%"
gh auth setup-git >nul 2>&1
gh repo create "!GITHUB_FULL_NAME!" --!GITHUB_VISIBILITY! --source . --remote origin --push
if errorlevel 1 (
    echo [WARN] GitHub repository creation failed.
    echo        Your local project is still valid.
    echo        Retry manually from:
    echo        cd /d "%GAME_ROOT%"
    echo        gh repo create !GITHUB_FULL_NAME! --!GITHUB_VISIBILITY! --source . --remote origin --push
) else (
    echo [OK] GitHub repository created:
    echo      !GITHUB_FULL_NAME!
)
popd

:finish
echo.
echo [OK] Game project created:
echo      %GAME_ROOT%
echo.
echo [STRUCTURE]
echo   %GAME_ROOT%\Project\%GAME_NAME%\%GAME_NAME%.vcxproj
echo   %GAME_ROOT%\Project\%GAME_NAME%\GameAssets\
echo.
echo [FOR SHARING WITH OTHER USERS]
echo   1. cd "%GAME_ROOT%"
echo   2. git submodule add ^<NEMEngine-Repo-URL^> External/NEMEngine
echo   3. del Premake\local_settings.lua
echo   4. del Premake\local_settings.bat
echo.

rd /s /q "%TEMP_DIR%" >nul 2>&1
endlocal
