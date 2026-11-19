# 크리스 1.0

**크리스 1.0**은 C++20과 DirectX 11 기반의 자체 3D 게임 엔진 및 에디터입니다.

---

## 주요 구성

| 경로 | 설명 |
| --- | --- |
| `Source/Runtime` | 엔진 코어, UObject/리플렉션, 렌더러, 씬/월드, 애니메이션, 오디오, 물리, Lua 스크립트(sol2) |
| `Source/Editor` | 크리스 1.0 에디터 (`KurisuEditor`) — 프로젝트 브라우저, 다중 뷰포트, 아웃라이너, 디테일, 스켈레탈/애니메이션/애님 블루프린트/UI 에디터 |
| `Source/Game` | 독립 실행형 게임 런타임 엔트리 (`KurisuGame`) |
| `Source/Programs/KurisuHeaderTool` | UCLASS/리플렉션 헤더 코드 생성기 (`KurisuHeaderTool`) |
| `EngineResources` | 엔진 아이콘(`KurisuLogo.ico`, `KurisuLogo.png`), 스플래시(`KurisuSplash.png`), HLSL 셰이더, 폰트 |
| `DefaultProject` | 기본 샘플 프로젝트 (`DefaultProject.kproject`) |
| `Setup` | Premake5 솔루션 생성 스크립트 및 게임 패키징 스크립트 |

> 참고: 솔루션 및 Visual Studio 프로젝트 명칭은 `Kurisu.slnx` / `KurisuEditor` / `KurisuEngine` / `KurisuGame` / `KurisuHeaderTool`이며, 내부 소스 디렉터리명(`Source/Editor/KurisuEd`, `Source/Programs/KurisuHeaderTool`)과 프로젝트 파일 확장자(`.kproject`)를 사용합니다.

---

## 빌드 및 실행 (Windows x64)

Visual Studio 2026의 C++ 데스크톱 개발 워크로드와 Windows SDK가 필요합니다. 솔루션 생성기는 Premake의 `vs2026` 타깃을 사용하며, Premake 실행 파일과 ImGui·FBX SDK 등 외부 라이브러리는 `Setup/Build` 및 `Source/ThirdParty`에 포함되어 있습니다.

1. **솔루션 및 프로젝트 생성**
   - 저장소 루트에서 `SlnGenerator.bat`을 실행하면 `Setup/Build/GenerateProjects.ps1`과 `premake5.exe`를 통해 루트 `Kurisu.slnx`와 `Build/Projects/*.vcxproj`가 생성됩니다.
2. **빌드**
   - `Kurisu.slnx`를 Visual Studio에서 열거나 MSBuild로 빌드합니다.
   - 지원 구성: `Debug`, `Release`, `Shipping` (`x64`, C++20)
   - 주요 타깃: `KurisuEditor` (에디터), `KurisuGame` (게임)
   - 에디터·엔진 빌드 결과물: `<EngineRoot>/Build/Bin/<Configuration>-windows-x86_64/`
   - 게임 실행 파일: 엔진 `Build/Bin/<Configuration>-windows-x86_64/KurisuGame.exe` (엔진 소스만으로 만들어지므로 모든 프로젝트가 공유)
3. **패키징**
   - `Setup/Packaging/Package.bat [Release|Shipping] [프로젝트.kproject 경로]` 또는 `Setup/Packaging/Package.ps1`을 실행하면 `<ProjectRoot>/Build/Package/<ProjectName>/`에 실행 파일, `EngineResources`, 프로젝트 `Content`/`Config`가 패키징됩니다.

저장소 루트에서 실행하는 PowerShell 예시입니다. `MSBuild.exe` 명령은 Visual Studio 개발자 PowerShell에서 사용할 수 있습니다.

```powershell
.\SlnGenerator.bat --no-pause
MSBuild.exe .\Kurisu.slnx -t:KurisuEditor -p:Configuration=Release -p:Platform=x64 -m
.\Build\Bin\Release-windows-x86_64\KurisuEditor.exe
```

에디터 디버깅 작업 디렉터리는 저장소 루트로 설정되어 있습니다. 독립 실행형 게임은 에디터의 패키징 기능이나 `Package.bat`으로 선택한 프로젝트에 빌드합니다. 다음은 기본 프로젝트를 패키징하고 실행하는 예시입니다.

```powershell
.\Setup\Packaging\Package.bat Release ".\DefaultProject\DefaultProject.kproject"
.\DefaultProject\Build\Package\DefaultProject\KurisuGame.exe
```

### 에디터 배포본

`SlnGenerator.bat`으로 솔루션을 갱신하고 `KurisuEditor`를 **Release / x64**로 빌드하면 `KurisuGame`도 먼저 빌드한 뒤 `Build/Distributions/KurisuEditor/` 배포 폴더를 자동 생성합니다. 폴더 전체를 옮겨 `KurisuEditor.exe`로 실행하면 됩니다. 에디터는 모든 구성에서 `KurisuGame` 다음에 빌드되며, 배포 폴더는 Release에서만 생성됩니다.

배포본에는 에디터 실행 파일, FBX DLL, 해당 도구 버전의 x64 C++ 런타임 DLL, `EngineResources` 및 게임 패키징 스크립트(`Setup/Packaging/Package.ps1`)가 포함됩니다. `EngineResources/Runtime/Windows-x86_64/KurisuGame.exe`는 기존 `KurisuGame`의 Release 빌드 결과이며, 사용자가 만든 콘텐츠와 함께 복사해 게임을 패키징합니다. 소스, 기본 프로젝트 콘텐츠, PDB, 정적 라이브러리, 중간 빌드 파일과 개인 설정은 포함하지 않습니다. 프로젝트는 새로 생성하거나 별도로 보관한 `.kproject`를 열면 됩니다.

원본 프로젝트에서 게임 패키징을 실행하면 `KurisuGame`을 엔진 `Build/Bin`에 증분 빌드한 뒤 그 실행 파일을 사용합니다. 실행 파일은 엔진 소스만으로 만들어지므로 모든 프로젝트가 같은 빌드를 공유합니다. 소스 없는 배포 에디터는 같은 `Package.ps1`을 빌드 없이 실행해 함께 제공되는 실행 템플릿으로 `<ProjectRoot>/Build/Package/<ProjectName>/KurisuGame.exe`와 콘텐츠를 생성하므로 Visual Studio나 엔진 소스가 필요하지 않습니다. 두 경우 모두 같은 스크립트가 복사하므로 패키지 구성은 같습니다.

### 게임 패키징 옵션

독립 실행형 게임은 커서를 자동으로 숨기거나 `Esc` 키로 종료하지 않습니다. 입력에 따른 게임 동작은 게임 로직에서 정합니다. 에디터의 PIE는 기존처럼 `Esc`로 재생을 중지합니다.

```powershell
.\Setup\Packaging\Package.bat Shipping ".\DefaultProject\DefaultProject.kproject"
# 이미 빌드된 실행 파일로 패키징할 때
.\Setup\Packaging\Package.ps1 -Configuration Release -Project ".\DefaultProject\DefaultProject.kproject" -SkipBuild
```

- 기본 구성은 `Release`이며, 프로젝트 경로를 생략하면 저장소 루트의 `.kproject`를 찾고, 없으면 `DefaultProject/DefaultProject.kproject`를 사용합니다.
- 기본 동작은 엔진 `Build/Bin/<Configuration>-windows-x86_64/`에 `KurisuGame`을 증분 빌드한 뒤 패키징입니다. 게임 프로젝트 폴더에는 패키지 출력(`Build/Package`) 외의 빌드 파일을 만들지 않습니다. `-SkipBuild`는 엔진 폴더에 해당 구성의 실행 파일이 이미 있을 때 사용합니다.
- 패키지에는 실행에 필요한 x64 C++ 런타임 DLL이 함께 들어가므로 Visual C++ 재배포 패키지가 없는 PC에서도 실행됩니다.
- 패키지에는 `.kproject`도 포함됩니다. `Config/editor.ini`, `Config/imgui.ini` 및 재생성 가능한 `*.obj.bin`은 제외하고, FBX·애니메이션의 `.bin` 에셋은 포함합니다.
- 같은 프로젝트의 기존 패키지 출력 폴더는 패키징할 때 교체됩니다.

---

## 프로젝트 썸네일

에디터를 실행하면 현재 Windows 사용자의 `.kproject` 탐색기 아이콘을 `KurisuBackground`로 자동 등록합니다. 새로 만든 프로젝트와 기존 프로젝트 모두에 적용되며, 아이콘은 에디터 실행 파일에 포함되어 배포본에서도 동작합니다. 연결 프로그램은 변경하지 않습니다.

- 새 프로젝트의 `Config`에는 `DefaultGame.ini`와 `EngineResources/Config`에서 복사한 기본 `editor.ini`, `imgui.ini`가 생성됩니다. 새 프로젝트의 렌더 설정과 창 배치는 이 템플릿에서 관리합니다.
- 대표 씬은 `Config/DefaultGame.ini`의 `[Game] GameDefaultMap`으로 지정합니다. 기본값은 `Content/Scenes/Main.scene`입니다.
- 프로젝트를 열면 대표 씬을 불러옵니다. 썸네일이 없으면 최초 화면에서 자동 생성하므로, 새 프로젝트에서 저장을 누르지 않고 종료해도 기본 씬의 썸네일이 남습니다.
- 대표 씬을 저장하면 현재 활성 뷰포트의 시점으로 `Saved/ProjectThumbnail.png`를 자동 갱신합니다. 기즈모, 그리드, 선택 외곽선은 제외합니다.
- 다른 씬을 저장하거나 PIE 중일 때는 갱신하지 않습니다. 다음에 프로젝트 선택 창을 열면 저장된 썸네일이 표시되며, 이미지가 없거나 읽을 수 없으면 엔진 로고를 표시합니다.

## 콘텐츠 드로어

- 하단 바의 `Content Drawer` 버튼 또는 `Ctrl + Space`로 열고 닫습니다. 별도 패널로 열려 있으면 단축키로 해당 패널에 포커스를 이동합니다.
- 폴더 트리와 경로 탐색, 뒤로/앞으로 이동으로 프로젝트 콘텐츠를 탐색합니다. 상단 `Import` 메뉴에서 정적 메시 또는 스켈레탈 메시 FBX를 임포트할 수 있습니다.
- 드로어 상단 경계를 드래그해 높이를 조절합니다. 하단 바에서 `Output Log`도 열 수 있습니다.
- 파일·폴더 삭제는 확인 창에서 확정합니다. 폴더 삭제 시 내부 콘텐츠도 디스크에서 삭제되며, 이 작업은 실행 취소할 수 없습니다.

---

## 에디터 주요 단축키

### 파일 및 플레이

- `Ctrl + N`: 새 씬
- `Ctrl + O`: 씬 열기
- `Ctrl + S` / `Ctrl + Alt + S`: 현재 씬 저장 / 다른 이름으로 저장
- `Alt + P`: Play (PIE)
- `Alt + S`: Simulate (SIE)
- `F8`: Eject / Possess 전환
- `Pause`: 일시정지 / 재개
- `Esc`: 플레이 중지

### 선택 및 편집

- **뷰포트 선택**: 좌클릭 단일 선택 / `Ctrl + 클릭` 다중 선택 토글
- **아웃라이너 선택**: 클릭 단일 선택 / `Ctrl + 클릭` 개별 토글 / `Shift + 클릭` 범위 선택
- `Ctrl + C` / `Ctrl + V`: 단일 및 다중 선택 액터 복사·붙여넣기 (선택 내부 부모-자식 계층 유지)
- `Ctrl + D`: 선택 액터 복제
- `Delete`: 선택 액터 삭제
- `F2`: 이름 변경
- `Ctrl + Z` / `Ctrl + Y`: 실행 취소 / 다시 실행 (레벨 에디터, 스켈레탈 메시 뷰어, 애니메이션 시퀀스 뷰어)

### 뷰포트 및 기즈모

- **우클릭 + `W A S D` / `Q E`**: 뷰포트 카메라 이동
- `F`: 선택한 액터(단일/다중)에 카메라 포커스
- `Space`: 기즈모 모드 전환 (이동 / 회전 / 크기)
- **`G`**: 기즈모 스냅 이동 켜기/끄기 토글 (상단 툴바 `Snap` 버튼 및 `Grid` 메뉴에서 스냅 크기/활성화 설정 가능, `editor.ini` 저장)

### 패널

- `Ctrl + Space`: 콘텐츠 드로어 열기/닫기 또는 열린 콘텐츠 패널에 포커스 (레벨 에디터 활성 상태, 텍스트 입력 중에는 동작하지 않음)
