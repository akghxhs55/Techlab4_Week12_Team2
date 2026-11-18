@echo off
setlocal DisableDelayedExpansion

REM Drop one .dmp file onto this batch file, or pass its path as an argument.
if "%~1"=="" goto usage
if not "%~2"=="" goto usage

powershell.exe -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0OpenKurisuDump.ps1" -DumpPath "%~f1"
set "Result=%ERRORLEVEL%"

if not "%Result%"=="0" pause
exit /b %Result%

:usage
echo Drop one .dmp file onto OpenKurisuDump.bat.
echo Usage: OpenKurisuDump.bat "C:\Crashes\Crash.dmp"
pause
exit /b 2
