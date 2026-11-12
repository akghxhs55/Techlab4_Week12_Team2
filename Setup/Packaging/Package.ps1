#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Shipping')][string]$Configuration = 'Release',
    [string]$Project,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
try {
    $engineRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    if ([string]::IsNullOrWhiteSpace($Project)) {
        $Project = Get-ChildItem -LiteralPath $engineRoot -Filter *.hproject -File | Select-Object -First 1 -ExpandProperty FullName
    }
    if ([string]::IsNullOrWhiteSpace($Project)) {
        $defaultProject = Join-Path $engineRoot 'DefaultProject\DefaultProject.hproject'
        if (Test-Path -LiteralPath $defaultProject -PathType Leaf) { $Project = $defaultProject }
    }
    if ([string]::IsNullOrWhiteSpace($Project)) { throw 'No .hproject file selected.' }
    $projectFile = (Resolve-Path -LiteralPath $Project).ProviderPath
    $projectRoot = Split-Path -Parent $projectFile
    $projectName = [IO.Path]::GetFileNameWithoutExtension($projectFile)
    $solution = Join-Path $engineRoot 'Hitori.slnx'
    if (-not $SkipBuild) {
        if (-not (Test-Path -LiteralPath $solution)) {
            & (Join-Path $engineRoot 'SlnGenerator.bat') --no-pause
            if ($LASTEXITCODE -ne 0) { throw 'Project generation failed.' }
        }
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        $msbuild = & $vswhere -latest -prerelease -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
        if (-not $msbuild) { throw 'MSBuild not found. Install the Visual Studio C++ workload.' }
        $env:VSLANG = '1033'
        & $msbuild $solution -t:HitoriGame "-p:Configuration=$Configuration" -p:Platform=x64 -m -nologo -v:minimal
        if ($LASTEXITCODE -ne 0) { throw 'Game build failed.' }
    }
    $gameExe = Join-Path $engineRoot "Build\Bin\$Configuration-windows-x86_64\HitoriGame.exe"
    if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) { throw "Missing executable: $gameExe" }
    $packageRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'Build\Package'))
    $out = [IO.Path]::GetFullPath((Join-Path $packageRoot $projectName))
    if (-not $out.StartsWith($packageRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Parent $out) -ne $packageRoot) { throw 'Invalid package output path.' }
    if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
    New-Item -ItemType Directory -Path $out -Force | Out-Null
    Copy-Item -LiteralPath $gameExe -Destination $out
    Copy-Item -LiteralPath $projectFile -Destination $out

    function Copy-Content([string]$Source, [string]$RelativeTarget) {
        if (-not (Test-Path -LiteralPath $Source -PathType Container)) { return }
        $sourceRoot = [IO.Path]::GetFullPath($Source).TrimEnd('\')
        $destination = Join-Path $out $RelativeTarget
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File) {
            if ($RelativeTarget -eq 'Config' -and $file.Name -in @('editor.ini', 'imgui.ini')) { continue }
            # OBJ bake files can be regenerated; FBX/animation .bin assets must ship.
            if ($file.Name.EndsWith('.obj.bin', [StringComparison]::OrdinalIgnoreCase)) { continue }
            $relative = $file.FullName.Substring($sourceRoot.Length + 1)
            $target = Join-Path $destination $relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $target
        }
    }
    Copy-Content (Join-Path $engineRoot 'EngineResources') 'EngineResources'
    $gameContent = Join-Path $projectRoot 'Content\Game'
    if (Test-Path -LiteralPath $gameContent) {
        Copy-Content $gameContent 'Content\Game'
    } else {
        Copy-Content (Join-Path $projectRoot 'Assets') 'Content\Game'
        Copy-Content (Join-Path $projectRoot 'Scenes') 'Content\Game\Scenes'
        Copy-Content (Join-Path $projectRoot 'Scripts') 'Content\Game\Scripts'
    }
    Copy-Content (Join-Path $projectRoot 'Config') 'Config'
    Write-Host "[Package] Done: $out"
    exit 0
} catch {
    [Console]::Error.WriteLine("[Package] $($_.Exception.Message)")
    exit 1
}
