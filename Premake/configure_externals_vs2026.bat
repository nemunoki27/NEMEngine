@echo off
setlocal

for %%I in ("%~dp0..") do set "ENGINE_ROOT=%%~fI"

set "ASSIMP_SRC=%ENGINE_ROOT%\Project\Externals\assimp"
set "ASSIMP_BUILD=%ENGINE_ROOT%\Generated\Externals\assimp"
set "ASSIMP_CACHE=%ENGINE_ROOT%\Premake\cmake\assimp-cache.cmake"

if not exist "%ASSIMP_SRC%\CMakeLists.txt" (
    echo [ERROR] assimp source not found:
    echo         %ASSIMP_SRC%
    exit /b 1
)

where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake was not found in PATH.
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
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%ENGINE_ROOT%\Premake\patch_assimp_vcxproj.ps1" -BuildRoot "%ASSIMP_BUILD%"
if errorlevel 1 (
    echo [ERROR] assimp project patch failed.
    exit /b 1
)

echo [OK] External projects configured.
endlocal
exit /b 0
