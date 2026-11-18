#requires -Version 5.1
$ErrorActionPreference = 'Stop'
$toolDirectory = Split-Path -Parent $PSScriptRoot
$projectRoot = (Resolve-Path -LiteralPath "$toolDirectory\..\..").Path
$testRoot = Join-Path $projectRoot ("Build\Tests\CrashDumpSymbols\" + [guid]::NewGuid())
[void][IO.Directory]::CreateDirectory($testRoot)
$validator = Join-Path $toolDirectory 'TestKurisuDumpSymbols.ps1'
Add-Type -Path (Join-Path $toolDirectory 'CrashDumpSymbols.cs')

function Set-UInt32([byte[]]$Bytes, [int]$Offset, [uint32]$Value) {
    [BitConverter]::GetBytes($Value).CopyTo($Bytes, $Offset)
}

function New-DumpFixture([string]$Path, [guid]$Signature, [uint32]$Age,
    [string]$ModuleName = 'KurisuEditor.exe') {
    $bytes = New-Object byte[] 1024
    Set-UInt32 $bytes 0 0x504d444d
    Set-UInt32 $bytes 4 0xa793
    Set-UInt32 $bytes 8 1
    Set-UInt32 $bytes 12 32
    Set-UInt32 $bytes 32 4
    Set-UInt32 $bytes 36 112
    Set-UInt32 $bytes 40 44
    Set-UInt32 $bytes 44 1
    Set-UInt32 $bytes (48 + 20) 160
    $moduleBytes = [Text.Encoding]::Unicode.GetBytes("C:\Build\$ModuleName")
    Set-UInt32 $bytes 160 $moduleBytes.Length
    $moduleBytes.CopyTo($bytes, 164)
    $pdbBytes = [Text.Encoding]::UTF8.GetBytes("C:\Build\KurisuEditor.pdb`0")
    Set-UInt32 $bytes (48 + 76) (24 + $pdbBytes.Length)
    Set-UInt32 $bytes (48 + 80) 320
    Set-UInt32 $bytes 320 0x53445352
    $Signature.ToByteArray().CopyTo($bytes, 324)
    Set-UInt32 $bytes 340 $Age
    $pdbBytes.CopyTo($bytes, 344)
    [IO.File]::WriteAllBytes($Path, $bytes)
}

function New-PdbFixture([string]$Path, [guid]$Signature, [uint32]$Age) {
    # Put a two-block directory in noncontiguous blocks to exercise MSF mapping.
    $bytes = New-Object byte[] 4096
    $magic = [Text.Encoding]::ASCII.GetBytes("Microsoft C/C++ MSF 7.00`r`n" +
        [char]0x1a + "DS`0`0`0")
    $magic.CopyTo($bytes, 0)
    Set-UInt32 $bytes 32 512
    Set-UInt32 $bytes 36 1
    Set-UInt32 $bytes 40 8
    Set-UInt32 $bytes 44 528
    Set-UInt32 $bytes 52 3
    Set-UInt32 $bytes (3 * 512) 4
    Set-UInt32 $bytes (3 * 512 + 4) 6
    $directory = New-Object byte[] 528
    Set-UInt32 $directory 0 130
    Set-UInt32 $directory 4 ([uint32]::MaxValue)
    Set-UInt32 $directory 8 28
    Set-UInt32 $directory 524 7
    [Array]::Copy($directory, 0, $bytes, 4 * 512, 512)
    [Array]::Copy($directory, 512, $bytes, 6 * 512, 16)
    Set-UInt32 $bytes (7 * 512) 20000404
    Set-UInt32 $bytes (7 * 512 + 8) $Age
    $Signature.ToByteArray().CopyTo($bytes, 7 * 512 + 12)
    [IO.File]::WriteAllBytes($Path, $bytes)
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Invoke-Validation([string]$Name, [int]$ExpectedExitCode,
    [string]$ExpectedMessage, [string]$SelectedDump = $script:dumpPath) {
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $result = & powershell.exe -NoProfile -ExecutionPolicy Bypass `
            -File $validator -DumpPath $SelectedDump -Store $script:storePath `
            -CacheDirectory $script:cachePath 2>&1
        $resultCode = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previousPreference }
    $resultText = $result | Out-String
    Assert-Condition ($resultCode -eq $ExpectedExitCode) "$Name returned $resultCode.`n$resultText"
    Assert-Condition ($resultText.Contains($ExpectedMessage)) "$Name missed diagnostic.`n$resultText"
    Write-Host "PASS: $Name"
}

$signature = [guid]'01234567-89ab-cdef-0123-456789abcdef'
$dumpPath = Join-Path $testRoot 'test crash ! 한글.dmp'
$storePath = Join-Path $testRoot 'symbol store'
$cachePath = Join-Path $testRoot 'symbol cache'
New-DumpFixture $dumpPath $signature 10
$identities = [Kurisu.Debugging.CrashDumpSymbols]::ReadDump($dumpPath)
Assert-Condition ($identities.Count -eq 1) 'Unexpected number of Kurisu modules.'
$identity = $identities[0]
Assert-Condition ($identity.StoreKey -eq '0123456789ABCDEF0123456789ABCDEFa') `
    'The RSDS GUID/Age or hexadecimal age key is incorrect.'
Write-Host 'PASS: minidump RSDS and hexadecimal Age extraction'
$relativePath = Join-Path 'KurisuEditor.pdb' (Join-Path $identity.StoreKey 'KurisuEditor.pdb')
$sharedPdb = Join-Path $storePath $relativePath
$cachedPdb = Join-Path $cachePath $relativePath
[void][IO.Directory]::CreateDirectory((Split-Path -Parent $sharedPdb))
New-PdbFixture $sharedPdb $signature 10
Invoke-Validation 'matching shared PDB is copied into the indexed cache' 0 'GUID/Age: MATCH'
Assert-Condition ((Get-FileHash -LiteralPath $sharedPdb).Hash -eq
    (Get-FileHash -LiteralPath $cachedPdb).Hash) 'The cache differs from the shared PDB.'

New-PdbFixture $sharedPdb ([guid]::NewGuid()) 10
Invoke-Validation 'a valid cache cannot hide a mismatched shared GUID' 1 'PDB mismatch'
New-PdbFixture $sharedPdb $signature 11
Invoke-Validation 'matching folder name cannot hide a mismatched shared Age' 1 'Age 10'

[IO.File]::Move($sharedPdb, "$sharedPdb.saved")
Invoke-Validation 'a valid cache cannot hide a missing shared PDB' 1 $identity.StoreKey
New-PdbFixture $sharedPdb $signature 10
New-PdbFixture $cachedPdb $signature 11
Invoke-Validation 'an invalid cache entry is replaced after shared verification' 0 'GUID/Age: MATCH'
Assert-Condition ((Get-FileHash -LiteralPath $sharedPdb).Hash -eq
    (Get-FileHash -LiteralPath $cachedPdb).Hash) 'Invalid cached data was not replaced.'

$sharedLock = [IO.File]::Open($sharedPdb, [IO.FileMode]::Open,
    [IO.FileAccess]::Read, [IO.FileShare]::None)
try {
    Invoke-Validation 'an unreadable shared PDB fails even with a valid cache' 1 'Could not verify and cache'
}
finally { $sharedLock.Dispose() }

[IO.File]::WriteAllBytes($sharedPdb, [byte[]](1, 2, 3))
Invoke-Validation 'a truncated shared PDB fails validation' 1 'truncated'
New-PdbFixture $sharedPdb $signature 10
$previousCachePath = $cachePath
try {
    $cachePath = Join-Path $testRoot 'cache-is-a-file'
    [IO.File]::WriteAllText($cachePath, 'not a directory')
    Invoke-Validation 'a local cache failure is distinguished from a missing shared PDB' `
        1 'Shared PDB identity matches, but cache preparation failed'
}
finally { $cachePath = $previousCachePath }
$badDump = Join-Path $testRoot 'bad-offset.dmp'
$badBytes = [IO.File]::ReadAllBytes($dumpPath)
Set-UInt32 $badBytes (48 + 20) 4096
[IO.File]::WriteAllBytes($badDump, $badBytes)
Invoke-Validation 'out-of-bounds minidump offsets are rejected' 1 'invalid offset' $badDump
$otherDump = Join-Path $testRoot 'other-process.dmp'
New-DumpFixture $otherDump $signature 10 'OtherApplication.exe'
Invoke-Validation 'an unrelated dump is not reported as verified' 1 'No KurisuEditor.exe' $otherDump
Write-Host "All symbol validation checks passed. Fixtures: $testRoot"
