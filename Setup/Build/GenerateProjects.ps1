#requires -Version 5.1
$ErrorActionPreference = 'Stop'
$engineRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$solution = Join-Path $engineRoot 'Kurisu.slnx'
try {
    $drive = 90..68 |
        ForEach-Object { '{0}:' -f [char]$_ } |
        Where-Object {
            [IO.Directory]::GetLogicalDrives() -notcontains "$_\"
        } |
        Select-Object -First 1

    if (-not $drive) {
        throw 'No free drive letter is available.'
    }

    & subst.exe $drive $engineRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Could not map the project to $drive"
    }

    try {
        Push-Location -LiteralPath "$drive\"
        try {
            $premake = "$drive\Setup\Build\premake5.exe"
            & $premake --file=Setup/Build/premake5.lua vs2026

            if ($LASTEXITCODE -ne 0) {
                throw "Premake failed: $LASTEXITCODE"
            }
        }
        finally {
            Pop-Location
        }

        $projectDirectory = Join-Path $engineRoot 'Build\Projects'
        $encoding = [Text.UTF8Encoding]::new($false)

        Get-ChildItem -LiteralPath $projectDirectory `
            -Filter '*.vcxproj*' -File | ForEach-Object {
            $content = [IO.File]::ReadAllText($_.FullName)
            $content = $content.Replace(
                "$drive\", '$(ProjectDir)..\..\')
            $content = $content.Replace(
                "$drive/", '$(ProjectDir)../../')
            [IO.File]::WriteAllText(
                $_.FullName, $content, $encoding)
        }

        if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) {
            throw "Solution was not generated: $solution"
        }
    }
    finally {
        & subst.exe $drive /D
    }
    Write-Host "Solution: $solution"
    Write-Host 'Open the root Kurisu.slnx in Visual Studio. Project files are under Build\Projects.'
    exit 0
}
catch {
    [Console]::Error.WriteLine("Project generation failed: $($_.Exception.Message)")
    exit 1
}
