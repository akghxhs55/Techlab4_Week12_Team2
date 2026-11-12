@echo off
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Package.ps1" -Configuration "%CONFIG%" -Project "%~2"
exit /b %ERRORLEVEL%
