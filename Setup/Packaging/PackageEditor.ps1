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
    if ([string]::IsNullOrWhiteSpace($VCToolsInstallDir)) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        $vs = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
        if (-not $vs) { throw 'Cannot locate the Visual C++ tools.' }
        $version = (Get-Content -LiteralPath (Join-Path $vs 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
        $VCToolsInstallDir = Join-Path $vs "VC\Tools\MSVC\$version"
    }
    $toolPath = [IO.Path]::GetFullPath($VCToolsInstallDir).TrimEnd('\')
    $toolVersion = Split-Path -Leaf $toolPath
    $vcRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $toolPath))
    $crtRoot = Join-Path $vcRoot "Redist\MSVC\$toolVersion\x64"
    $crt = Get-ChildItem -LiteralPath $crtRoot -Directory | Where-Object { $_.Name -match '^Microsoft\.VC\d+\.CRT$' } | Select-Object -First 1
    if (-not $crt) { throw "Cannot locate the redistributable x64 CRT matching $toolVersion." }
    $crtFiles = @{}
    foreach ($dll in Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll' -File) { $crtFiles[$dll.Name] = $dll }
    $dumpbin = Join-Path $toolPath 'bin\Hostx64\x64\dumpbin.exe'
    if (-not (Test-Path -LiteralPath $dumpbin)) { throw 'Cannot locate dumpbin for runtime dependency inspection.' }
    $pending = New-Object 'System.Collections.Generic.Queue[string]'
    foreach ($name in $binaries) { $pending.Enqueue((Join-Path $bin $name)) }
    $pending.Enqueue($gameExe)
    $required = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    while ($pending.Count -gt 0) {
        $dependencies = & $dumpbin /NOLOGO /DEPENDENTS $pending.Dequeue()
        if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect binary dependencies.' }
        foreach ($line in $dependencies) {
            if ($line -match '^\s+([\w.-]+\.dll)\s*$' -and $crtFiles.ContainsKey($Matches[1])) {
                $name = $Matches[1]
                if ($required.Add($name)) { $pending.Enqueue($crtFiles[$name].FullName) }
            }
        }
    }
    $runtimeDlls = @($required | ForEach-Object { $crtFiles[$_] })
    if ($runtimeDlls.Count -eq 0) { throw 'No redistributable CRT dependencies found.' }
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
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -Recurse -Force }
    Move-Item -LiteralPath $stage -Destination $output
    Write-Host "Portable editor: $output"
    exit 0
} catch {
    [Console]::Error.WriteLine("Editor distribution failed: $($_.Exception.Message)")
    exit 1
}
