# 개발 도구

| 폴더 | 용도 |
|---|---|
| `Build` | `premake5.lua`, Premake 실행 파일, 솔루션 생성 스크립트 |
| `Packaging` | 에디터 패키징 메뉴와 수동 실행에서 사용하는 배치·PowerShell |
| `Validation` | 에디터·게임 초기화, 2초 실행, 정상 종료 확인 |

저장소 루트에서 실행한다. 다른 작업 폴더에서 실행할 때는 스크립트의 절대 경로를 사용한다.

```powershell
# 루트에 Hitori.slnx 생성
.\SlnGenerator.bat

# 기본 프로젝트 Release 빌드·패키징
.\Setup\Packaging\Package.bat Release

# 빌드 없이 기본 프로젝트 패키징
powershell -NoProfile -ExecutionPolicy Bypass -File Setup/Packaging/Package.ps1 -SkipBuild

# 기본 프로젝트 패키지 실행 확인 (패키징 후)
powershell -NoProfile -ExecutionPolicy Bypass -File Setup/Validation/SmokeTest.ps1

# Debug 에디터 실행 확인 (빌드 후)
powershell -NoProfile -ExecutionPolicy Bypass -File Setup/Validation/SmokeTest.ps1 -Executable Build/Bin/Debug-windows-x86_64/HitoriEditor.exe -Project DefaultProject/DefaultProject.hproject
```

솔루션은 루트, 생성 프로젝트는 `Build/Projects`, 패키지는 프로젝트의 `Build/Package`, 검증 결과는 엔진의 `Build/Tests`에 저장한다.
삭제된 C++ 테스트 소스를 참조하던 `TestPaths.ps1`·`TestAssets.ps1`은 제거했다.
