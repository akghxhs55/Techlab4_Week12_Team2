param(
    # 팀 심볼 저장소의 공유 폴더 경로
    [Parameter(Mandatory)][string]$Store,

    # EXE/DLL/PDB의 게시 기록에 남길 빌드 ID
    [Parameter(Mandatory)][string]$BuildId,

    # 어느 빌드 구성의 EXE/DLL/PDB를 게시할지 선택한다. 기본값은 Debug다.
    [ValidateSet('Debug', 'Release', 'Shipping')][string]$Configuration = 'Debug',

    # PATH에서 symstore.exe를 찾는다. 필요하면 전체 경로를 넘길 수 있다.
    [string]$SymStorePath = 'symstore.exe'
)

# 빌드가 끝난 뒤 호출해 출력 폴더의 EXE/DLL/PDB를 SymStore에 직접 등록한다.
# SymStore가 파일 이름과 식별자로 저장 위치를 정하며, WinDbg가 필요한 버전을 찾는다.
# 위 param 블록은 배치 파일 또는 PowerShell에서 전달받을 인자와 기본값을 정의한다.

# PowerShell 명령에서 오류가 발생하면 스크립트를 중단한다.
$ErrorActionPreference = 'Stop'

# 스크립트가 있는 Setup\CrashDump에서 두 단계 올라가 저장소 루트를 구한다.
# 배치 파일을 다른 작업 디렉터리에서 실행해도 같은 프로젝트의 출력물을 찾는다.
$RepoRoot = (Resolve-Path -LiteralPath "$PSScriptRoot\..\..").Path

# 선택한 빌드 구성의 x64 출력 폴더를 찾는다.
$Bin = Join-Path $RepoRoot "Build\Bin\${Configuration}-windows-x86_64"

# Get-Command는 지정한 경로나 PATH에서 실행 파일을 찾는다.
# Source 속성으로 전체 경로를 얻으며, 설치되지 않았다면 여기서 오류로 중단한다.
$SymStore = (Get-Command $SymStorePath -CommandType Application).Source

# 두 EXE와 대응 PDB, 에디터에 필요한 FBX DLL을 먼저 확인한다.
# 빌드가 끝난 출력물을 사용해야 EXE와 PDB의 버전이 서로 일치한다.
# LiteralPath는 경로의 특수 문자를 와일드카드로 해석하지 않는다.
# Get-Item은 파일 정보를 반환하며, 필수 파일이 하나라도 없으면 중단한다.
$RequiredFiles = Get-Item -LiteralPath "$Bin\KurisuEditor.exe", "$Bin\KurisuEditor.pdb", "$Bin\KurisuGame.exe", "$Bin\KurisuGame.pdb", "$Bin\libfbxsdk.dll"

# 출력 폴더의 모든 DLL을 등록 대상에 넣는다. @()는 결과를 배열로 다룬다.
$Dlls = @(Get-ChildItem -LiteralPath $Bin -File -Filter '*.dll')

# DLL에 대응하는 PDB도 있으면 등록한다. foreach에서 출력한 파일 정보가 수집된다.
$DllPdbs = foreach ($Dll in $Dlls) {
    # 예를 들어 Plugin.dll의 경로에서 확장자만 바꿔 Plugin.pdb를 찾는다.
    $DllPdbPath = [IO.Path]::ChangeExtension($Dll.FullName, '.pdb')
    # DLL의 PDB는 존재하는 경우에만 추가한다.
    if (Test-Path -LiteralPath $DllPdbPath -PathType Leaf) {
        Get-Item -LiteralPath $DllPdbPath
    }
}

# 필수 파일과 추가 DLL/PDB 목록을 합치고, 이름이 같은 항목은 한 번만 등록한다.
$BuildFiles = @($RequiredFiles) + $Dlls + @($DllPdbs) | Sort-Object -Property Name -Unique

# 저장소 루트 폴더를 준비한다. [void]는 반환되는 폴더 정보의 콘솔 출력을 생략한다.
[void][IO.Directory]::CreateDirectory($Store)

# 여러 PC가 같은 SymStore 이력을 동시에 수정하지 않도록 게시 중 잠금을 유지한다.
# FileShare.None으로 연 파일은 프로세스 종료 시에도 자동으로 잠금이 해제된다.
$LockPath = Join-Path $Store '.publish.lock'
try {
    # 잠금 파일을 생성하거나 연다. 다른 게시자가 같은 파일을 열고 있으면 실패한다.
    $PublishLock = [IO.File]::Open($LockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
}
catch {
    # 권한 부족이나 동시 게시 등으로 잠금을 얻지 못하면 등록 전에 중단한다.
    throw "Cannot lock the symbol store. Check write permissions or retry after another publisher finishes. $($_.Exception.Message)"
}

# 파일 이름을 키로 사용해 열어 둔 입력 스트림을 관리한다.
$SourceStreams = @{}

# 모든 입력 파일을 읽기 전용으로 열어 게시 도중 다른 빌드가 변경하지 못하게 한다.
try {
    foreach ($BuildFile in $BuildFiles) {
        # FileShare.Read는 SymStore의 읽기를 허용하고 쓰기와 삭제를 제한한다.
        $SourceStreams[$BuildFile.Name] = [IO.File]::Open($BuildFile.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    }

    # 빌드 출력 폴더의 원본 파일을 하나씩 SymStore에 직접 등록한다.
    foreach ($BuildFile in $BuildFiles) {
        # &는 변수에 저장한 실행 파일을 호출한다.
        # /f는 입력 파일, /s는 심볼 저장소, /t는 제품명, /v는 게시 이력의 빌드 ID다.
        # PDB는 GUID/Age, EXE와 DLL은 이미지의 타임스탬프/크기로 저장 위치를 정한다.
        & $SymStore add /f $BuildFile.FullName /s $Store /t Kurisu /v $BuildId

        # 외부 프로그램의 오류는 종료 코드로 확인한다. 앞선 등록이 성공했을 수 있다.
        # 중간에 실패하면 같은 빌드 출력물을 사용해 게시를 다시 실행할 수 있다.
        if ($LASTEXITCODE -ne 0) {
            throw "SymStore failed for $($BuildFile.Name), exit code $LASTEXITCODE. Earlier files may already be registered."
        }
    }

    # 원시 UNC 경로를 사용하는 디버거도 SymSrv 저장소로 인식하게 한다.
    $MarkerPath = Join-Path $Store 'pingme.txt'
    if (-not (Test-Path -LiteralPath $MarkerPath -PathType Leaf)) {
        [IO.File]::WriteAllText($MarkerPath, '')
    }

    # 모든 파일 등록이 성공했을 때 저장소 주소와 완료 메시지를 출력한다.
    Write-Output "Symbol store: $Store"
    Write-Output "Build ID: $BuildId"
    Write-Output 'KurisuEditor and KurisuGame EXE/DLL/PDB files published successfully.'
}
finally {
    # 중간 오류가 있어도 열어 둔 모든 입력 파일을 닫아 빌드 도구가 다시 쓰게 한다.
    foreach ($SourceStream in $SourceStreams.Values) {
        $SourceStream.Dispose()
    }

    # 성공하거나 오류로 중단돼도 잠금을 해제해 다음 게시가 진행되게 한다.
    # .publish.lock 파일은 남아 있어도 열려 있지 않으면 잠금 상태가 아니다.
    $PublishLock.Dispose()
}
