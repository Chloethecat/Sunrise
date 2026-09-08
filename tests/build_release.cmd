@echo off
setlocal
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
    echo Run this script from an x64 Visual Studio Developer Command Prompt.
    exit /b 1
)
where msbuild >nul 2>&1
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
msbuild Sunrise.sln /m:4 /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo /fl /flp:logfile=build-release.log
exit /b %errorlevel%
