@echo off
setlocal

REM Apply debugger settings through the console utility without starting the IDE.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0SetupKurisuDebugger.ps1"
set "SetupExitCode=%ERRORLEVEL%"

if not "%SetupExitCode%"=="0" (
    if /I not "%~1"=="--no-pause" pause
)
exit /b %SetupExitCode%
