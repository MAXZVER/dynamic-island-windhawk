@echo off
setlocal enabledelayedexpansion

rem Builds the standalone island with the Microsoft toolchain, for machines that
rem have Visual Studio Build Tools but not the mingw-w64 clang that build.bat
rem expects. Same sources, different compiler:
rem   -include  -> /FI      -municode/-mwindows -> /SUBSYSTEM:WINDOWS + wWinMain
rem   -static   -> /MT      -O2                 -> /O2
rem The mingw build stays the reference; this one exists so a one-line fix does
rem not have to wait for a toolchain install.
rem
rem Every path is quoted on assignment: "Program Files (x86)" carries brackets
rem that cmd would otherwise read as block syntax.

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%VCVARS%" (
    echo Visual Studio Build Tools were not found at:
    echo   "%VCVARS%"
    exit /b 1
)

pushd "%~dp0"

echo [1/3] Setting up the Microsoft toolchain...
call "%VCVARS%" >nul
if errorlevel 1 goto :failed

echo [2/3] Generating sources from the mod...
python gen_island.py
if errorlevel 1 goto :failed

echo [3/3] Compiling...
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 ^
   /DUNICODE /D_UNICODE /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /DNOMINMAX ^
   /I. /FI wh_api.h ^
   main.cpp wh_api.cpp island.cpp settings_ui.cpp ^
   /Fe:DynamicIsland.exe ^
   /link /SUBSYSTEM:WINDOWS ^
   advapi32.lib ole32.lib oleaut32.lib shcore.lib d2d1.lib dwrite.lib dwmapi.lib ^
   gdi32.lib user32.lib shell32.lib runtimeobject.lib windowscodecs.lib avrt.lib ^
   setupapi.lib winhttp.lib pdh.lib uuid.lib comctl32.lib
if errorlevel 1 goto :failed

del /q *.obj 2>nul
echo.
echo Built DynamicIsland.exe with MSVC
popd
exit /b 0

:failed
echo.
echo BUILD FAILED
popd
exit /b 1
