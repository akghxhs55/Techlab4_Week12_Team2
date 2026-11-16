#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Release')][string]$Configuration = 'Release',
    [string]$VCToolsInstallDir
)
$ErrorActionPreference = 'Stop'
try {
    $engineRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $bin = Join-Path $engineRoot "Build\Bin\$Configuration-windows-x86_64"
    # The editor's Release project reference builds the launcher template here.
    $gameExe = Join-Path $bin 'KurisuGame.exe'
    if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) {
        throw 'Missing KurisuGame.exe. Regenerate the solution and build KurisuEditor in Release.'
    }
    $distributionRoot = [IO.Path]::GetFullPath((Join-Path $engineRoot 'Build\Distributions'))
    $stage = [IO.Path]::GetFullPath((Join-Path $distributionRoot '.KurisuEditor-Release.staging'))
    $output = [IO.Path]::GetFullPath((Join-Path $distributionRoot 'KurisuEditor'))
    foreach ($path in @($stage, $output)) {
        if ((Split-Path -Parent $path) -ne $distributionRoot) { throw 'Invalid distribution output path.' }
    }
    $binaries = @('KurisuEditor.exe', 'libfbxsdk.dll')
    foreach ($name in $binaries) {
        if (-not (Test-Path -LiteralPath (Join-Path $bin $name) -PathType Leaf)) {
            throw "Missing $name. Build the corresponding Release target first."
        }
    }
    . (Join-Path $PSScriptRoot 'VCRuntime.ps1')
    $inspected = @($binaries | ForEach-Object { Join-Path $bin $_ }) + $gameExe
    $runtimeDlls = Get-VCRuntimeDlls -Binaries $inspected -VCToolsInstallDir $VCToolsInstallDir
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    foreach ($name in $binaries) {
        Copy-Item -LiteralPath (Join-Path $bin $name) -Destination $stage
    }
    foreach ($dll in $runtimeDlls) { Copy-Item -LiteralPath $dll.FullName -Destination $stage }
    # EngineResources contains the shipped shaders, images, fonts and project templates.
    Copy-Item -LiteralPath (Join-Path $engineRoot 'EngineResources') -Destination $stage -Recurse
    $playerDirectory = Join-Path $stage 'EngineResources\Runtime\Windows-x86_64'
    New-Item -ItemType Directory -Path $playerDirectory -Force | Out-Null
    Copy-Item -LiteralPath $gameExe -Destination $playerDirectory
    # The editor packages games with the same script as a source checkout.
    $packagingDirectory = Join-Path $stage 'Setup\Packaging'
    New-Item -ItemType Directory -Path $packagingDirectory -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Package.ps1') -Destination $packagingDirectory
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -Recurse -Force }
    Move-Item -LiteralPath $stage -Destination $output
    Write-Host "Portable editor: $output"
    exit 0
} catch {
    [Console]::Error.WriteLine("Editor distribution failed: $($_.Exception.Message)")
    exit 1
}
