# Hitori Engine

엔진 코드·공용 자원·게임 콘텐츠·생성 파일을 분리한 12주차 작업용 프로젝트입니다. 기본 프로젝트는 빈 씬으로 시작합니다.

```text
Hitori.slnx               # 솔루션 진입점; SlnGenerator.bat으로 생성
EngineResources/          # 엔진 공용 자원: Shaders, Icons, Fonts, Splash, Scripts 등
DefaultProject/           # 기본 게임 프로젝트
  DefaultProject.hproject
  Config/DefaultGame.ini
  Content/Game/           # 프로젝트가 만드는 콘텐츠
    Scenes/Main.scene
    Scripts/
  Build/                  # 이 프로젝트의 캐시·에디터 설정·패키지
Source/
  Runtime/                # 엔진 런타임
  Editor/                 # 에디터, FBX SDK 임포터, Windows 리소스 스크립트
  Game/                   # 게임 실행 진입점
  Programs/               # 코드 생성 등 보조 프로그램
  ThirdParty/             # 소스로 포함한 외부 라이브러리와 FBX SDK
Setup/                    # 프로젝트 생성·패키징·검증 도구 (Setup/README.md 참고)
  Build/                  # Premake와 솔루션 생성 스크립트
  Packaging/              # 패키징 배치·PowerShell
  Validation/             # 에디터·게임 실행 확인
Build/                    # 전부 생성 데이터; Git에서 제외
  Projects/               # vcxproj, filters 등 생성 프로젝트
  Generated/              # 리플렉션 generated.h
  Intermediate/           # obj, pch, tlog 등 컴파일 중간 파일
  Bin/                    # exe, lib, dll, pdb
  Cache/                  # Shaders, Meshes
  Saved/                  # 최근 프로젝트·에디터 설정
  Package/                # 배포 결과
  Tests/                  # 테스트 실행 파일·fixture·보고서
  Temp/                   # 일회성 로그
```

## 빌드

1. 솔루션 생성: `SlnGenerator.bat` (`Setup/Build/premake5.lua` 사용)
2. 루트의 `Hitori.slnx`를 열어 Debug 또는 Release 빌드
3. 에디터 실행 파일: `Build\Bin\Debug-windows-x86_64\HitoriEditor.exe`

생성 프로젝트나 캐시를 `Source`·`Content`에 추가하지 않습니다. 솔루션과 프로젝트 파일은 저장소에 커밋하지 않습니다.

ImGui와 FBX SDK를 포함한 의존성은 `Source/ThirdParty`의 일반 파일로 포함합니다. 서브모듈 초기화나 별도 FBX 설치·환경 변수 설정이 필요하지 않으며 솔루션 생성 시 Git·네트워크를 호출하지 않습니다. FBX SDK 2020.3.11의 Windows x64 동적 링크 파일만 포함하고 에디터에서 사용합니다. 원본 라이선스는 해당 폴더에 보존합니다.

## 실행 설정

`<프로젝트>/Config/DefaultGame.ini`는 독립 게임(`HitoriGame.exe`)의 기본 설정입니다. 기본 프로젝트의 파일은 `DefaultProject/Config/DefaultGame.ini`입니다. 열린 `.hproject`가 있는 프로젝트 폴더에서 읽고, 패키징할 때도 함께 복사합니다.

- `[Game]`: 시작 씬 `GameDefaultMap`
- `[Window]`: 창 제목, 해상도, 테두리 없는 창, VSync
- `[Rendering]`: 조명 모델·타일 컬링, FXAA, 그림자, 감마, 비네트·카메라 오버레이

게임 초기화 시 읽으며 실행 중 자동 갱신하지 않습니다. 없는 항목은 코드의 기본값을 사용합니다. 새 프로젝트 생성 시에도 이 파일을 만듭니다. 에디터의 개인 설정은 이 파일과 별개로 `Build/Saved/Editor/editor.ini`에 저장합니다.

## 콘텐츠와 캐시

게임 에셋은 `Content/Game`에, 엔진이 직접 사용하는 공용 에셋은 `EngineResources`에 둡니다. 씬과 Lua도 게임 콘텐츠입니다. 외부 프로젝트를 열어도 엔진 자원은 원래 엔진 폴더에서 읽습니다.

- `Engine/Icons/Point.png`: 엔진 가상 경로 → `EngineResources/Icons/Point.png`
- `Models/Cube.obj`: 게임 콘텐츠 상대 키 → `<프로젝트>/Content/Game/Models/Cube.obj`
- `Content/Game/Models/Cube.obj`: 프로젝트 기준 파일 경로
- 예전 `Assets/...`, `Scenes/...`, `Scripts/...` 경로도 공통 로더에서 지원합니다.
- 이전 엔진 경로 `Content/Engine/...`도 `EngineResources/...`로 해석합니다. 저장된 `Engine/...` 가상 경로는 그대로 사용할 수 있습니다.
- `.obj.bin`은 재생성 가능한 캐시이므로 `Build/Cache/Meshes`에만 만듭니다.
- 임포트된 `.fbx.bin`, `.anim.bin`은 실제 실행용 에셋입니다. 새로 임포트할 때는 `Content/Game/FBX`에 저장합니다.
- 셰이더 캐시는 `<엔진>/Build/Cache/Shaders`, 에디터 설정은 `<프로젝트>/Build/Saved/Editor`에 저장합니다.

샘플 에셋과 예전 데모 씬은 제거했습니다. 기본 Pawn은 내장 Sphere를 사용하고, C++ 애니메이션 예제는 특정 샘플 파일을 자동 로드하지 않습니다. 사용하는 애니메이션 경로를 지정하거나 Anim Blueprint를 연결합니다.

## 패키징과 검증

```powershell
# DefaultProject를 빌드하고 패키징
.\Setup\Packaging\Package.bat Release

# 외부 프로젝트
.\Setup\Packaging\Package.bat Release "D:\Games\Example\Example.hproject"

# 이미 빌드한 실행 파일로 패키징만 수행
powershell -NoProfile -ExecutionPolicy Bypass -File Setup/Packaging/Package.ps1 -SkipBuild

```

패키지는 `<프로젝트>/Build/Package/<프로젝트 파일명>`에 생성됩니다. `EngineResources`, 게임 콘텐츠·Config·프로젝트 파일을 포함하며 개발용 캐시와 중간 파일은 포함하지 않습니다. 기본 출력은 `DefaultProject/Build/Package/DefaultProject`입니다.
