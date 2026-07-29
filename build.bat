@echo off
setlocal

REM ============================================================================
REM  build.bat - сборка компилятора Zenith
REM ----------------------------------------------------------------------------
REM  Что было сломано в старой версии:
REM
REM  1) echo с текстом "(g++)" стоял ВНУТРИ блока ( ... ).
REM     cmd видит первую ) как конец блока, остаток строки "or MSVC (cl)."
REM     становится командой -> "Непредвиденное появление: or."
REM     Здесь скобки в тексте экранированы: ^( и ^)
REM
REM  2) %ERRORLEVEL% внутри блока ( ... ) раскрывается ОДИН РАЗ при разборе
REM     всего блока, до выполнения where. Проверка не работала.
REM     Здесь блоков нет вообще - структура на метках goto.
REM
REM  3) main.cpp использует std::filesystem. MSVC по умолчанию это C++14,
REM     нужен явный /std:c++17, иначе сборка через cl падает.
REM ============================================================================

echo Building Zenith Compiler...

set "SRC=src/main.cpp src/lexer.cpp src/parser.cpp src/codegen.cpp src/codegen_pe.cpp src/codegen_builtins.cpp src/codegen_gui.cpp src/codegen_dx11.cpp src/codegen_sw.cpp src/optimizer.cpp"
set "SRCW=src\main.cpp src\lexer.cpp src\parser.cpp src\codegen.cpp src\codegen_pe.cpp src\codegen_builtins.cpp src\codegen_gui.cpp src\codegen_dx11.cpp src\codegen_sw.cpp src\optimizer.cpp"

REM --- проверка, что запускаемся из корня проекта ---
if not exist "src\main.cpp" goto no_src

REM --- если CXX задан снаружи, используем его ---
if defined CXX goto have_cxx

REM --- ищем g++ ---
where g++ >nul 2>nul
if not errorlevel 1 set "CXX=g++" & goto have_cxx

REM --- ищем cl ---
where cl >nul 2>nul
if not errorlevel 1 set "CXX=cl" & goto have_cxx

goto no_compiler

:have_cxx
echo Using compiler: %CXX%

if /i "%CXX%"=="cl"      goto build_msvc
if /i "%CXX%"=="cl.exe"  goto build_msvc
goto build_gcc

:build_gcc
%CXX% -std=c++17 -O2 -o zenith.exe %SRC%
if errorlevel 1 goto failed
goto success

:build_msvc
%CXX% /nologo /EHsc /std:c++17 /O2 /Fezenith.exe /Isrc %SRCW%
if errorlevel 1 goto failed
goto success

:success
echo.
echo Success: zenith.exe created
endlocal
exit /b 0

:failed
echo.
echo Build failed!
endlocal
exit /b 1

:no_compiler
echo Error: No C++ compiler found.
echo Install MinGW ^(g++^) or MSVC ^(cl^).
echo.
echo For MSVC run this script from "x64 Native Tools Command Prompt for VS".
endlocal
exit /b 1

:no_src
echo Error: src\main.cpp not found.
echo Run this script from the Zenith project root folder.
endlocal
exit /b 1
