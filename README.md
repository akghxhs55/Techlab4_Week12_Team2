# 크리스 1.0 (Kris Engine 1.0)

**크리스 1.0 (Kris Engine 1.0)** 은 C++20과 DirectX 기반의 자체 3D 게임 엔진 및 에디터입니다.

---

## 주요 구성

| 경로 | 설명 |
| --- | --- |
| `Source/Runtime` | 엔진 코어, UObject/리플렉션, 렌더러, 씬/월드, 애니메이션, 오디오, 물리, Lua 스크립트(sol2) |
| `Source/Editor` | 크리스 1.0 에디터 (`KrisEditor`) — 프로젝트 브라우저, 다중 뷰포트, 아웃라이너, 디테일, 스켈레탈/애니메이션/애님 블루프린트/UI 에디터 |
| `Source/Game` | 독립 실행형 게임 런타임 엔트리 (`KrisGame`) |
| `Source/Programs/HitoriHeaderTool` | UCLASS/리플렉션 헤더 코드 생성기 (`KrisHeaderTool`) |
| `EngineResources` | 엔진 아이콘(`Kris.ico`, `KrisLogo.png`), 스플래시(`KrisSplash.png`), HLSL 셰이더, 폰트 |
| `DefaultProject` | 기본 샘플 프로젝트 (`DefaultProject.hproject`) |
| `Setup` | Premake5 솔루션 생성 스크립트 및 게임 패키징 스크립트 |

> 참고: 솔루션 및 Visual Studio 프로젝트 명칭은 `Kris.slnx` / `KrisEditor` / `KrisEngine` / `KrisGame` / `KrisHeaderTool`이며, 내부 소스 디렉터리명(`Source/Editor/HitoriEd`, `Source/Programs/HitoriHeaderTool`)과 프로젝트 파일 확장자(`.hproject`)는 호환성을 위해 유지됩니다.

---

## 빌드 및 실행 (Windows x64)

1. **솔루션 및 프로젝트 생성**
   - 저장소 루트에서 `SlnGenerator.bat`을 실행하면 `Setup/Build/GenerateProjects.ps1`과 `premake5.exe`를 통해 루트 `Kris.slnx`와 `Build/Projects/*.vcxproj`가 생성됩니다.
2. **빌드**
   - `Kris.slnx`를 Visual Studio에서 열거나 MSBuild로 빌드합니다.
   - 지원 구성: `Debug`, `Release`, `Shipping` (`x64`, C++20)
   - 주요 타깃: `KrisEditor` (에디터), `KrisGame` (게임)
   - 빌드 결과물: `Build/Bin/<Configuration>-windows-x86_64/`
3. **패키징**
   - `Setup/Packaging/Package.bat [Release|Shipping] [프로젝트.hproject 경로]` 또는 `Setup/Packaging/Package.ps1`을 실행하면 `<ProjectRoot>/Build/Package/<ProjectName>/`에 실행 파일, `EngineResources`, 프로젝트 `Content`/`Config`가 패키징됩니다.

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
