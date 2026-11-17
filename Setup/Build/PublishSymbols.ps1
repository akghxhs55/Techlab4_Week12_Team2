param(
    # 팀 심볼 저장소의 공유 폴더 경로
    [Parameter(Mandatory)][string]$Store,

    # 두 PDB의 게시 기록에 남길 빌드 ID
    [Parameter(Mandatory)][string]$BuildId,

    # 어느 빌드 구성의 PDB를 게시할지 선택한다. 기본값은 Debug다.
    [ValidateSet('Debug', 'Release', 'Shipping')][string]$Configuration = 'Debug',

    # PATH에서 symstore.exe를 찾는다. 필요하면 전체 경로를 넘길 수 있다.
    [string]$SymStorePath = 'symstore.exe'
)

# PowerShell 명령에서 오류가 발생하면 스크립트를 중단한다.
$ErrorActionPreference = 'Stop'

# 스크립트가 있는 Setup\Build에서 두 단계 올라가 저장소 루트를 구한다.
$RepoRoot = (Resolve-Path -LiteralPath "$PSScriptRoot\..\..").Path

# 선택한 빌드 구성의 x64 출력 폴더를 찾는다.
$Bin = Join-Path $RepoRoot "Build\Bin\${Configuration}-windows-x86_64"

# SymStore 실행 파일의 실제 경로를 구한다.
$SymStore = (Get-Command $SymStorePath -CommandType Application).Source

# 두 PDB를 먼저 확인한다. 파일이 빠졌으면 게시를 시작하기 전에 중단한다.
$Pdbs = Get-Item -LiteralPath "$Bin\KurisuEditor.pdb", "$Bin\KurisuGame.pdb"

foreach ($Pdb in $Pdbs) {
    # /f는 게시할 PDB, /s는 저장소, /t는 제품명, /v는 빌드 ID다.
    # SymStore는 PDB를 복사하고 파일 자체의 식별자로 저장 위치를 정한다.
    & $SymStore add /f $Pdb.FullName /s $Store /t Kurisu /v $BuildId

    # 외부 프로그램의 종료 코드를 확인해 실패한 PDB를 알려준다.
    if ($LASTEXITCODE -ne 0) {
        throw "SymStore failed for $($Pdb.Name), exit code $LASTEXITCODE."
    }
}
