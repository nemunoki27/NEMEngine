@echo off
setlocal

pushd "%~dp0"

for %%I in ("%~dp0..") do set "ENGINE_ROOT=%%~fI"

set "ASSIMP_SRC=%ENGINE_ROOT%\Project\Externals\assimp"
set "ASSIMP_BUILD=%ENGINE_ROOT%\Generated\Externals\assimp"
set "ASSIMP_CACHE=%ENGINE_ROOT%\Premake\cmake\assimp-cache.cmake"

if not exist "%ASSIMP_SRC%\CMakeLists.txt" (
    echo [ERROR] assimp source not found:
    echo         %ASSIMP_SRC%
    popd
    exit /b 1
)

where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake was not found in PATH.
    popd
    exit /b 1
)

mkdir "%ASSIMP_BUILD%" >nul 2>&1

echo ===== Configure assimp =====
cmake -S "%ASSIMP_SRC%" -B "%ASSIMP_BUILD%" -G "Visual Studio 18 2026" -A x64 -C "%ASSIMP_CACHE%" ^
 -D BUILD_SHARED_LIBS=OFF ^
 -D ASSIMP_BUILD_ZLIB=ON ^
 -D ASSIMP_BUILD_ASSIMP_TOOLS=OFF ^
 -D ASSIMP_BUILD_SAMPLES=OFF ^
 -D ASSIMP_BUILD_TESTS=OFF ^
 -D ASSIMP_INSTALL=OFF ^
 -D ASSIMP_NO_EXPORT=ON ^
 -D ASSIMP_WARNINGS_AS_ERRORS=OFF

if errorlevel 1 (
    echo [ERROR] assimp configure failed.
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