@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "SETTINGS_FILE=%SCRIPT_DIR%NEMEngine.NoHotReload.vssettings"
set "MODE=import"

if not "%~1"=="" (
    if /I "%~1"=="--reset" (
        set "MODE=reset"
    ) else if /I "%~1"=="--check" (
        set "MODE=check"
    ) else (
        echo [ERROR] Unexpected argument: %~1
        echo Usage: %~nx0 [--check^|--reset]
        exit /b 1
    )
)

if not "%~2"=="" (
    echo [ERROR] Unexpected argument: %~2
    echo Usage: %~nx0 [--check^|--reset]
    exit /b 1
)

echo ===== NEMEngine Visual Studio Debug Settings =====

if not exist "%SETTINGS_FILE%" (
    echo [ERROR] Settings file was not found:
    echo         %SETTINGS_FILE%
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe was not found.
    echo         Install Visual Studio Installer, or verify Visual Studio is installed.
    exit /b 1
)

set "VS_INSTALL="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do (
    set "VS_INSTALL=%%I"
)

if not defined VS_INSTALL (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do (
        set "VS_INSTALL=%%I"
    )
)

if not defined VS_INSTALL (
    echo [ERROR] Visual Studio installation was not found by vswhere.
    exit /b 1
)

set "DEVENV=%VS_INSTALL%\Common7\IDE\devenv.exe"
if not exist "%DEVENV%" (
    echo [ERROR] devenv.exe was not found:
    echo         %DEVENV%
    exit /b 1
)

echo [INFO] Visual Studio: %DEVENV%
echo [INFO] Settings file: %SETTINGS_FILE%

if /I "%MODE%"=="check" (
    echo [OK] Visual Studio and settings file were found.
    echo      No settings were imported.
    exit /b 0
)

tasklist /FI "IMAGENAME eq devenv.exe" 2>NUL | find /I "devenv.exe" >NUL
if not errorlevel 1 (
    echo [ERROR] Visual Studio is currently running.
    echo         Close all Visual Studio windows, then run this script again.
    exit /b 1
)

if /I "%MODE%"=="reset" (
    echo [WARN] Applying settings with /ResetSettings.
    echo        This can affect more IDE settings than the default import mode.
    "%DEVENV%" /ResetSettings "%SETTINGS_FILE%"
) else (
    echo [INFO] Importing settings.
    "%DEVENV%" /Command "Tools.ImportandExportSettings /import:%SETTINGS_FILE%"
)

if errorlevel 1 (
    echo [ERROR] Visual Studio settings import failed.
    exit /b 1
)

echo [OK] Visual Studio debug settings were applied.
echo      Restart Visual Studio before opening NEMEngine.
echo      The settings take effect from the next debug session.
exit /b 0
