@echo off
setlocal DisableDelayedExpansion

REM Register the .dmp handler for the current Windows user.
powershell.exe -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0RegisterKurisuDump.ps1"
set "Result=%ERRORLEVEL%"

pause
exit /b %Result%
