@echo off
setlocal

pushd "%~dp0"

for %%I in ("%~dp0..") do set "ENGINE_ROOT=%%~fI"

call "%~dp0configure_externals_vs2026.bat"
if errorlevel 1 (
    popd
    exit /b 1
)

echo ===== Cleanup Old Project Files =====
if exist "%ENGINE_ROOT%\Project\NEMEngine.vcxproj" del /q "%ENGINE_ROOT%\Project\NEMEngine.vcxproj"
if exist "%ENGINE_ROOT%\Project\NEMEngine.vcxproj.filters" del /q "%ENGINE_ROOT%\Project\NEMEngine.vcxproj.filters"
if exist "%ENGINE_ROOT%\Project\NEMEngine.vcxproj.user" del /q "%ENGINE_ROOT%\Project\NEMEngine.vcxproj.user"

if exist "%ENGINE_ROOT%\Project\Sandbox.vcxproj" del /q "%ENGINE_ROOT%\Project\Sandbox.vcxproj"
if exist "%ENGINE_ROOT%\Project\Sandbox.vcxproj.filters" del /q "%ENGINE_ROOT%\Project\Sandbox.vcxproj.filters"
if exist "%ENGINE_ROOT%\Project\Sandbox.vcxproj.user" del /q "%ENGINE_ROOT%\Project\Sandbox.vcxproj.user"

echo ===== Generate Start =====
premake5.exe --file="%~dp0premake5.lua" vs2026 > premake_error.log 2>&1
set "PREMAKE_RC=%ERRORLEVEL%"

type premake_error.log
echo.

findstr /c:"Error:" premake_error.log >nul
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
popd
endlocal
exit /b 0
