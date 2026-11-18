@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup\Build\GenerateProjects.ps1"
set "GenerateExitCode=%ERRORLEVEL%"

REM Apply debugger settings without opening a Visual Studio window.
if not "%GenerateExitCode%"=="0" goto Finish
call "%~dp0Setup\CrashDump\SetupKurisuDebugger.bat" --no-pause
set "GenerateExitCode=%ERRORLEVEL%"

:Finish
echo.
if /I not "%~1"=="--no-pause" pause
exit /b %GenerateExitCode%
