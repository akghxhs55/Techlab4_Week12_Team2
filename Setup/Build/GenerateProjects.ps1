#requires -Version 5.1
$ErrorActionPreference = 'Stop'
$engineRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$solution = Join-Path $engineRoot 'Hitori.slnx'
try {
    Push-Location -LiteralPath $engineRoot
    try {
        & (Join-Path $PSScriptRoot 'premake5.exe') "--file=$PSScriptRoot\premake5.lua" vs2026
        if ($LASTEXITCODE -ne 0) { throw "Premake failed with exit code $LASTEXITCODE." }
        if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) { throw "Solution was not generated: $solution" }
    }
    finally { Pop-Location }
    Write-Host "Solution: $solution"
    Write-Host 'Open the root Hitori.slnx in Visual Studio. Project files are under Build\Projects.'
    exit 0
}
catch {
    [Console]::Error.WriteLine("Project generation failed: $($_.Exception.Message)")
    exit 1
}
