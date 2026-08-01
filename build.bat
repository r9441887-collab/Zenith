@echo off
REM ==============================================================
REM Zenith Compiler Build Script (for GCC)
REM ==============================================================

echo Building Zenith Compiler with GCC...

REM Check for g++
where g++ >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: GCC not found! Please install MinGW-w64 or add to PATH
    echo Download: https://www.mingw-w64.org/
    pause
    exit /b 1
)

echo Using GCC...

REM Create output directory
if not exist build mkdir build

REM Compile the compiler with GCC
echo Compiling...

g++ -std=c++17 -O2 -Wall -Wextra ^
   -I. ^
   src/lexer.cpp ^
   src/parser.cpp ^
   src/codegen.cpp ^
   src/codegen_builtins.cpp ^
   src/codegen_gui.cpp ^
   src/codegen_dx11.cpp ^
   src/codegen_dx11_shaders.cpp ^
   src/codegen_efi.cpp ^
   src/codegen_bios.cpp ^
   src/codegen_pe.cpp ^
   src/codegen_sw.cpp ^
   src/optimizer.cpp ^
   src/main.cpp ^
   -o build/zenith.exe ^
   -static

if %errorlevel% neq 0 (
    echo.
    echo BUILD FAILED!
    echo.
    pause
    exit /b 1
)

echo.
echo ==============================================================
echo BUILD SUCCESSFUL!
echo Compiler: build\zenith.exe
echo ==============================================================
echo.

REM Show usage
echo Usage:
echo   build\zenith.exe input.zenith -o output.exe
echo.
echo App types:
echo   app efi      - UEFI application (with GOP support)
echo   app bios     - BIOS/bootloader application
echo   app bare     - Bare metal application
echo   app gui      - Windows GUI application
echo   app console  - Windows console application
echo.
echo Kernel modes (for efi/bios/bare):
echo   kernel_mode: independent - No boot services after entry
echo   kernel_mode: dependent   - Uses boot services directly
echo.

pause
