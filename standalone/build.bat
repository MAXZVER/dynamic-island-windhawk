@echo off
setlocal

rem Builds the standalone island with the toolchain Windhawk already ships,
rem so the code is compiled by the exact compiler it was written against.

set WH=C:\Program Files\Windhawk\Compiler
set CLANG=%WH%\bin\clang++.exe

if not exist "%CLANG%" (
    echo Windhawk's compiler was not found at %CLANG%
    echo Install Windhawk, or point CLANG at any mingw-w64 clang++.
    exit /b 1
)

pushd "%~dp0"

echo [1/2] Generating sources from the mod...
python gen_island.py || (popd & exit /b 1)

echo [2/2] Compiling...
"%CLANG%" ^
    -std=c++23 -O2 -municode -mwindows -static ^
    -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 ^
    -target x86_64-w64-mingw32 ^
    -I"%WH%\include" -I. ^
    -include wh_api.h ^
    main.cpp wh_api.cpp island.cpp ^
    -o DynamicIsland.exe ^
    -ladvapi32 -lole32 -loleaut32 -lshcore -ld2d1 -ldwrite -ldwmapi -lgdi32 ^
    -luser32 -lshell32 -lruntimeobject -lwindowscodecs -lavrt -lsetupapi ^
    -lwinhttp -lpdh -luuid -lcomctl32 || (popd & exit /b 1)

echo.
echo Built DynamicIsland.exe
echo Settings are written to %%APPDATA%%\DynamicIsland\config.ini on first run.
popd
