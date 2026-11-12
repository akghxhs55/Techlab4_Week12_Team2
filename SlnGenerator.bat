@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup\Build\GenerateProjects.ps1"
set "GenerateExitCode=%ERRORLEVEL%"
echo.
if /I not "%~1"=="--no-pause" pause
exit /b %GenerateExitCode%
