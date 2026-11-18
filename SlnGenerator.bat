@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup\Build\GenerateProjects.ps1"
set "GenerateExitCode=%ERRORLEVEL%"

REM 솔루션 생성이 성공하면 현재 사용자의 VS 2026 디버거 설정을 가져온다.
REM 설정 파일에는 공유 심볼 경로, 활성화 값, 캐시, Source Link 값만 들어 있다.
if not "%GenerateExitCode%"=="0" goto Finish
call "%~dp0Setup\CrashDump\SetupKurisuDebugger.bat" --no-pause
set "GenerateExitCode=%ERRORLEVEL%"

:Finish
echo.
if /I not "%~1"=="--no-pause" pause
exit /b %GenerateExitCode%
