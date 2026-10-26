@echo off
rem Builds the fix (32-bit, static CRT) into out\: dsound.dll (loads on its own) and
rem HitmanGiveAllFix.asi (for an ASI loader). Both come from the same dllmain.cpp.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio with C++ tools was not found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>&1 || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
if not exist out\obj mkdir out\obj
set CFLAGS=/nologo /W4 /O2 /MT /EHsc /std:c++17 /I. /Foout\obj\
cl %CFLAGS% /LD dllmain.cpp dsound_proxy.cpp /Feout\dsound.dll /link /DEF:dsound.def || exit /b 1
cl %CFLAGS% /LD dllmain.cpp /Feout\HitmanGiveAllFix.asi || exit /b 1
