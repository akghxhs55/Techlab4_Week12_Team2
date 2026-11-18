@echo off
setlocal

REM Call the publishing script next to this batch file.
REM PublishSymbols.ps1 locates the project root two levels above its directory.
set "Store=\\172.21.10.22\KurisuSymbols"

REM Prefer the SDK installation, then fall back to SymStore on PATH.
set "SymStore=%ProgramFiles(x86)%\Windows Kits\10\Debuggers\x64\symstore.exe"
if not exist "%SymStore%" set "SymStore=symstore.exe"

REM Identify this publication using the computer name and UTC timestamp.
for /f %%T in ('powershell.exe -NoProfile -Command "[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')"') do (
    set "BuildId=%COMPUTERNAME%-Release-%%T"
)

REM Register the build output EXE, DLL, and PDB files with SymStore.
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned ^
    -File "%~dp0PublishSymbols.ps1" ^
    -Store "%Store%" -BuildId "%BuildId%" ^
    -Configuration Release -SymStorePath "%SymStore%"

REM Preserve the publishing result and keep the window open.
set "Result=%ERRORLEVEL%"
if not "%Result%"=="0" echo Symbol publishing failed. Exit code: %Result%
pause
exit /b %Result%
