@echo off
setlocal


REM --- 1. Set your vcpkg root here ----
set VCPKG_ROOT=C:\vcpkg

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo [INFO] vcpkg not found at %VCPKG_ROOT%. Cloning...
    git clone https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
    call "%VCPKG_ROOT%\bootstrap-vcpkg.bat"
)

REM --- 2. Install dependencies --------
echo [INFO] Installing vcpkg packages (this may take a while on first run)...
"%VCPKG_ROOT%\vcpkg.exe" install curl nlohmann-json libpqxx libharu --triplet x64-windows

REM --- 3. Configure & build -----------
if not exist build mkdir build
cd build

cmake .. ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DCMAKE_BUILD_TYPE=Release

if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

cmake --build . --config Release

if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo [OK] Build complete.  Run:  build\Release\crypto_analytics.exe
endlocal
