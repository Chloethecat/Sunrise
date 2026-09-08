@echo off
setlocal
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
    echo Run this script from an x64 Visual Studio Developer Command Prompt.
    exit /b 1
)
where cl >nul 2>&1
if errorlevel 1 exit /b 1
where lib >nul 2>&1
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist "build\quest-tests" mkdir "build\quest-tests"
rem Reuse the full Release build's production objects; no game initialization is called.
lib /nologo /out:build\quest-tests\sunrise-test.lib build\obj\x64\Release\*.obj
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /MT /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I"Sunrise\src" /I"Sunrise\vendor\sqlite" /Fo"build\quest-tests\\" /Fe"build\quest-tests\quest_initialization_test.exe" tests\quest_initialization_test.cpp build\quest-tests\sunrise-test.lib /link /LTCG /OPT:REF /STACK:16777216 kernel32.lib bcrypt.lib user32.lib gdi32.lib dwmapi.lib d3dcompiler.lib shell32.lib ws2_32.lib synchronization.lib ole32.lib windowscodecs.lib
if errorlevel 1 exit /b 1
"build\quest-tests\quest_initialization_test.exe" "%CD%" %*
exit /b %errorlevel%
