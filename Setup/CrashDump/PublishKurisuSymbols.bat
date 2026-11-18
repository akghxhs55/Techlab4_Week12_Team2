@echo off
setlocal

REM 이 배치 파일과 같은 폴더의 게시 스크립트를 호출한다.
REM PublishSymbols.ps1이 자신의 위치에서 두 단계 위의 프로젝트 루트를 찾는다.
set "Store=\\172.21.10.22\KurisuSymbols"

REM SDK 기본 경로를 먼저 사용하고, 없으면 PATH에서 SymStore를 찾는다.
set "SymStore=%ProgramFiles(x86)%\Windows Kits\10\Debuggers\x64\symstore.exe"
if not exist "%SymStore%" set "SymStore=symstore.exe"

REM PC 이름과 UTC 시각으로 이번 게시를 식별한다.
for /f %%T in ('powershell.exe -NoProfile -Command "[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')"') do (
    set "BuildId=%COMPUTERNAME%-Debug-%%T"
)

REM 빌드 출력 폴더의 EXE, DLL, PDB를 SymStore에 직접 등록한다.
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned ^
    -File "%~dp0PublishSymbols.ps1" ^
    -Store "%Store%" -BuildId "%BuildId%" ^
    -Configuration Debug -SymStorePath "%SymStore%"

REM 게시 결과를 보존한 뒤 창을 열어 둔다.
set "Result=%ERRORLEVEL%"
if not "%Result%"=="0" echo Symbol publishing failed. Exit code: %Result%
pause
exit /b %Result%
