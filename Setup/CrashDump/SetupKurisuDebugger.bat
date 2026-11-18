@echo off
setlocal

REM 같은 폴더의 설정 파일에서 공유 심볼 경로, 캐시, Source Link 설정을 가져온다.
REM SlnGenerator.bat에서도 호출하므로 --no-pause이면 오류 시 키 입력을 기다리지 않는다.
set "SettingsFile=%~dp0KurisuDebugging.vssettings"
set "VSWhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

REM 설정 파일과 Visual Studio 검색 도구의 존재를 확인한다.
if not exist "%SettingsFile%" (
    echo KurisuDebugging.vssettings was not found.
    if /I not "%~1"=="--no-pause" pause
    exit /b 1
)
if not exist "%VSWhere%" (
    echo Visual Studio Installer's vswhere.exe was not found.
    if /I not "%~1"=="--no-pause" pause
    exit /b 1
)

REM VS 2026의 내부 버전은 18.x다. 설치된 IDE의 실행 경로를 구한다.
set "DevEnv="
for /f "usebackq delims=" %%V in (`"%VSWhere%" -latest -version "[18.0,19.0)" -property productPath`) do (
    set "DevEnv=%%V"
)
if not defined DevEnv (
    echo Visual Studio 2026 was not found.
    if /I not "%~1"=="--no-pause" pause
    exit /b 1
)

REM IDE가 시작된 뒤 설정 가져오기 명령을 실행한다. 파일에 포함된 네 설정값을 갱신한다.
start "" "%DevEnv%" /Command "Tools.ImportandExportSettings /import:\"%SettingsFile%\""
REM 이 종료 코드는 Visual Studio 실행 요청의 결과이며 설정 적용 완료 여부는 IDE에서 확인한다.
exit /b %ERRORLEVEL%
