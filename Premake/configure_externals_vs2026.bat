@echo off
setlocal

for %%I in ("%~dp0..") do set "ENGINE_ROOT=%%~fI"

set "ASSIMP_SRC=%ENGINE_ROOT%\Project\Externals\assimp"
set "ASSIMP_BUILD=%ENGINE_ROOT%\Generated\Externals\assimp"
set "ASSIMP_CACHE=%ENGINE_ROOT%\Premake\cmake\assimp-cache.cmake"
set "LIBCURL_SRC=%ENGINE_ROOT%\Project\Externals\libcurl"
set "LIBCURL_BUILD=%ENGINE_ROOT%\Generated\Externals\libcurl"
set "LIBCURL_CACHE=%ENGINE_ROOT%\Premake\cmake\libcurl-cache.cmake"

if not exist "%ASSIMP_SRC%\CMakeLists.txt" (
    echo [ERROR] assimp source not found:
    echo         %ASSIMP_SRC%
    exit /b 1
)

if not exist "%LIBCURL_SRC%\CMakeLists.txt" (
    echo [ERROR] libcurl source not found:
    echo         %LIBCURL_SRC%
    exit /b 1
)

where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake was not found in PATH.
    exit /b 1
)

mkdir "%ASSIMP_BUILD%" >nul 2>&1
mkdir "%LIBCURL_BUILD%" >nul 2>&1

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

powershell -NoProfile -ExecutionPolicy Bypass -File "%ENGINE_ROOT%\Premake\patch_cmake_vcxproj.ps1" -BuildRoot "%ASSIMP_BUILD%"
if errorlevel 1 (
    echo [ERROR] assimp project patch failed.
    exit /b 1
)

echo ===== Configure libcurl =====
cmake -S "%LIBCURL_SRC%" -B "%LIBCURL_BUILD%" -G "Visual Studio 18 2026" -A x64 -C "%LIBCURL_CACHE%" ^
 -D BUILD_SHARED_LIBS=OFF ^
 -D BUILD_STATIC_LIBS=ON ^
 -D CURL_STATIC_CRT=ON ^
 -D BUILD_CURL_EXE=OFF ^
 -D BUILD_EXAMPLES=OFF ^
 -D BUILD_TESTING=OFF ^
 -D BUILD_LIBCURL_DOCS=OFF ^
 -D BUILD_MISC_DOCS=OFF ^
 -D ENABLE_CURL_MANUAL=OFF ^
 -D CURL_DISABLE_INSTALL=ON ^
 -D CURL_USE_SCHANNEL=ON ^
 -D CURL_USE_OPENSSL=OFF ^
 -D CURL_USE_MBEDTLS=OFF ^
 -D CURL_USE_WOLFSSL=OFF ^
 -D CURL_USE_GNUTLS=OFF ^
 -D CURL_USE_RUSTLS=OFF ^
 -D CURL_USE_CMAKECONFIG=OFF ^
 -D CURL_USE_PKGCONFIG=OFF ^
 -D CURL_ZLIB=OFF ^
 -D CURL_BROTLI=OFF ^
 -D CURL_ZSTD=OFF ^
 -D CURL_USE_LIBPSL=OFF ^
 -D CURL_USE_LIBSSH2=OFF ^
 -D CURL_USE_LIBSSH=OFF ^
 -D USE_LIBIDN2=OFF ^
 -D CURL_DISABLE_LDAP=ON ^
 -D CURL_DISABLE_LDAPS=ON

if errorlevel 1 (
    echo [ERROR] libcurl configure failed.
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%ENGINE_ROOT%\Premake\patch_cmake_vcxproj.ps1" -BuildRoot "%LIBCURL_BUILD%"
if errorlevel 1 (
    echo [ERROR] libcurl project patch failed.
    exit /b 1
)

echo [OK] External projects configured.
endlocal
exit /b 0
