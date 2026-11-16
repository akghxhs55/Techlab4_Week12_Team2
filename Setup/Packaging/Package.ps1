#requires -Version 5.1
# The only implementation of game packaging. The editor runs this script both
# from a source checkout and from a portable editor distribution, so packaged
# games always have the same layout; only where KurisuGame.exe comes from differs.
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Shipping')][string]$Configuration = 'Release',
    [string]$Project,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
try {
    $engineRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    # A portable editor distribution ships this script without sources or build tools.
    $isDistribution = -not (Test-Path -LiteralPath (Join-Path $engineRoot 'Setup\Build\premake5.lua') -PathType Leaf)
    if ([string]::IsNullOrWhiteSpace($Project)) {
        $Project = Get-ChildItem -LiteralPath $engineRoot -Filter *.kproject -File | Select-Object -First 1 -ExpandProperty FullName
    }
    if ([string]::IsNullOrWhiteSpace($Project)) {
        $defaultProject = Join-Path $engineRoot 'DefaultProject\DefaultProject.kproject'
        if (Test-Path -LiteralPath $defaultProject -PathType Leaf) { $Project = $defaultProject }
    }
    if ([string]::IsNullOrWhiteSpace($Project)) { throw 'No .kproject file selected.' }
    $projectFile = (Resolve-Path -LiteralPath $Project).ProviderPath
    $projectRoot = Split-Path -Parent $projectFile
    $projectName = [IO.Path]::GetFileNameWithoutExtension($projectFile)

    if ($isDistribution) {
        # The distribution carries the Release launcher it was built with, and
        # the C++ runtime DLLs beside KurisuEditor.exe cover that launcher too.
        if ($Configuration -ne 'Release') { throw 'A portable editor can only package Release games.' }
        $gameExe = Join-Path $engineRoot 'EngineResources\Runtime\Windows-x86_64\KurisuGame.exe'
        if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) {
            throw 'The editor distribution is missing its player template. Replace it with a complete Release distribution.'
        }
        $runtimeDlls = @(Get-ChildItem -LiteralPath $engineRoot -Filter '*.dll' -File |
            Where-Object { $_.Name -match '^(msvcp|vcruntime|concrt)' })
    } else {
        $solution = Join-Path $engineRoot 'Kurisu.slnx'
        if (-not $SkipBuild) {
            $gameProject = Join-Path $engineRoot 'Build\Projects\KurisuGame.vcxproj'
            if (-not (Test-Path -LiteralPath $solution) -or
                -not (Test-Path -LiteralPath $gameProject) -or
                # Older generated projects redirected the launcher into each game project.
                ([IO.File]::ReadAllText($gameProject).Contains('$(KurisuGameProjectDir)'))) {
                & (Join-Path $engineRoot 'SlnGenerator.bat') --no-pause
                if ($LASTEXITCODE -ne 0) { throw 'Project generation failed.' }
            }
            $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
            $msbuild = & $vswhere -latest -prerelease -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
            if (-not $msbuild) { throw 'MSBuild not found. Install the Visual Studio C++ workload.' }
            $env:VSLANG = '1033'
            # The launcher is built from engine sources only, so every project shares
            # one incremental build in the engine's Build folder.
            & $msbuild $solution -t:KurisuGame "-p:Configuration=$Configuration" -p:Platform=x64 -m -nologo -v:minimal
            if ($LASTEXITCODE -ne 0) { throw 'Game build failed.' }
        }
        $gameExe = Join-Path $engineRoot "Build\Bin\$Configuration-windows-x86_64\KurisuGame.exe"
        if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) { throw "Missing executable: $gameExe" }
        . (Join-Path $PSScriptRoot 'VCRuntime.ps1')
        $runtimeDlls = @(Get-VCRuntimeDlls -Binaries @($gameExe))
    }
    if ($runtimeDlls.Count -eq 0) { throw 'No C++ runtime DLLs found for the game.' }

    $packageRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'Build\Package'))
    $out = [IO.Path]::GetFullPath((Join-Path $packageRoot $projectName))
    if (-not $out.StartsWith($packageRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Parent $out) -ne $packageRoot) { throw 'Invalid package output path.' }
    if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
    New-Item -ItemType Directory -Path $out -Force | Out-Null
    Copy-Item -LiteralPath $gameExe -Destination (Join-Path $out 'KurisuGame.exe')
    foreach ($dll in $runtimeDlls) { Copy-Item -LiteralPath $dll.FullName -Destination $out }
    Copy-Item -LiteralPath $projectFile -Destination $out

    function Copy-Content([string]$Source, [string]$RelativeTarget) {
        if (-not (Test-Path -LiteralPath $Source -PathType Container)) { return }
        $sourceRoot = [IO.Path]::GetFullPath($Source).TrimEnd('\')
        $destination = Join-Path $out $RelativeTarget
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File) {
            $relative = $file.FullName.Substring($sourceRoot.Length + 1)
            # Player templates are editor support files, not game assets.
            if ($RelativeTarget -eq 'EngineResources' -and $relative.StartsWith('Runtime\', [StringComparison]::OrdinalIgnoreCase)) { continue }
            if ($RelativeTarget -eq 'Config' -and $file.Name -in @('editor.ini', 'imgui.ini')) { continue }
            # OBJ bake files can be regenerated; FBX/animation .bin assets must ship.
            if ($file.Name.EndsWith('.obj.bin', [StringComparison]::OrdinalIgnoreCase)) { continue }
            $target = Join-Path $destination $relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $target
        }
    }
    Copy-Content (Join-Path $engineRoot 'EngineResources') 'EngineResources'
    $gameContent = Join-Path $projectRoot 'Content'
    if (Test-Path -LiteralPath $gameContent) {
        Copy-Content $gameContent 'Content'
    } else {
        Copy-Content (Join-Path $projectRoot 'Assets') 'Content'
        Copy-Content (Join-Path $projectRoot 'Scenes') 'Content\Scenes'
        Copy-Content (Join-Path $projectRoot 'Scripts') 'Content\Scripts'
    }
    Copy-Content (Join-Path $projectRoot 'Config') 'Config'
    Write-Host "[Package] Done: $out"
    exit 0
} catch {
    [Console]::Error.WriteLine("[Package] $($_.Exception.Message)")
    exit 1
}
