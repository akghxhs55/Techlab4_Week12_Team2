#requires -Version 5.1
$ErrorActionPreference = 'Stop'
$engineRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$solution = Join-Path $engineRoot 'Kurisu.slnx'
$temporaryDrive = $null
try {
    # Generate through an ASCII drive alias, then keep generated paths relative.
    $temporaryDrive = 90..68 |
        ForEach-Object { '{0}:' -f [char]$_ } |
        Where-Object {
            [IO.Directory]::GetLogicalDrives() -notcontains "$_\"
        } |
        Select-Object -First 1

    if (-not $temporaryDrive) {
        throw 'No free drive letter is available.'
    }

    & subst.exe $temporaryDrive $engineRoot
    if ($LASTEXITCODE -ne 0) {
        $temporaryDrive = $null
        throw 'Could not create the temporary project path.'
    }

    Push-Location -LiteralPath "$temporaryDrive\"
    try {
        $premake = "$temporaryDrive\Setup\Build\premake5.exe"
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
            "$temporaryDrive\", '$(ProjectDir)..\..\')
        $content = $content.Replace(
            "$temporaryDrive/", '$(ProjectDir)../../')
        # Premake also emits the drive root without a trailing separator.
        $content = $content.Replace($temporaryDrive, '$(ProjectDir)..\..')
        if ($content.Contains($temporaryDrive)) {
            throw "Temporary drive reference remains in $($_.Name)."
        }
        [IO.File]::WriteAllText(
            $_.FullName, $content, $encoding)
    }

    if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) {
        throw "Solution was not generated: $solution"
    }

    # Solution paths are relative to the repository, not to ProjectDir.
    $solutionContent = [IO.File]::ReadAllText($solution)
    $solutionContent = $solutionContent.Replace("$temporaryDrive\", '')
    $solutionContent = $solutionContent.Replace("$temporaryDrive/", '')
    [IO.File]::WriteAllText($solution, $solutionContent, $encoding)

    [xml]$solutionDocument = $solutionContent
    $projectReferences = $solutionDocument.SelectNodes(
        '//Project/@Path | //BuildDependency/@Project')
    foreach ($projectReference in $projectReferences) {
        $projectPath = Join-Path $engineRoot $projectReference.Value
        if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
            throw "Solution references a missing project: $projectPath"
        }
    }
    Write-Host "Solution: $solution"
    Write-Host 'Open the root Kurisu.slnx in Visual Studio. Project files are under Build\Projects.'
    exit 0
}
catch {
    [Console]::Error.WriteLine("Project generation failed: $($_.Exception.Message)")
    exit 1
}
finally {
    if ($temporaryDrive) { & subst.exe $temporaryDrive /D }
}
