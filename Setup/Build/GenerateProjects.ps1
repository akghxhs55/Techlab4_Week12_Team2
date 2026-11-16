#requires -Version 5.1
$ErrorActionPreference = 'Stop'
$engineRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$solution = Join-Path $engineRoot 'Kurisu.slnx'
$temporaryDrive = $null
try {
    $generationRoot = $engineRoot
    # This Premake build cannot open Lua scripts through non-ASCII paths.
    # Use an ASCII alias only during generation, then restore real paths in XML.
    if ($engineRoot -match '[^\x00-\x7F]') {
        $temporaryDrive = @('Z:', 'Y:', 'X:', 'W:', 'V:', 'U:', 'T:', 'S:', 'R:') |
            Where-Object { -not (Test-Path -LiteralPath ($_ + '\')) } |
            Select-Object -First 1
        if (-not $temporaryDrive) { throw 'No free drive letter for project generation.' }
        & subst.exe $temporaryDrive $engineRoot
        if ($LASTEXITCODE -ne 0) {
            $temporaryDrive = $null
            throw 'Could not create the temporary project path.'
        }
        $generationRoot = $temporaryDrive + '\'
    }
    Push-Location -LiteralPath $generationRoot
    try {
        & '.\Setup\Build\premake5.exe' '--file=Setup/Build/premake5.lua' vs2026
        if ($LASTEXITCODE -ne 0) { throw "Premake failed with exit code $LASTEXITCODE." }
        if ($temporaryDrive) {
            $generatedFiles = @(Get-Item -LiteralPath $solution)
            $generatedFiles += Get-ChildItem -LiteralPath (Join-Path $engineRoot 'Build\Projects') -File |
                Where-Object { $_.Name -match '\.vcxproj(\.filters|\.user)?$' }
            $utf8 = New-Object System.Text.UTF8Encoding($false)
            $xmlRoot = [System.Security.SecurityElement]::Escape($engineRoot.TrimEnd('\'))
            foreach ($file in $generatedFiles) {
                $text = [IO.File]::ReadAllText($file.FullName)
                $text = $text.Replace($temporaryDrive + '\', $xmlRoot + '\')
                $text = $text.Replace($temporaryDrive + '/', $xmlRoot.Replace('\', '/') + '/')
                # Premake strips the slash from drive-root values (debugdir,
                # include directories and filters), leaving a bare "Z:".
                $text = $text.Replace($temporaryDrive, $xmlRoot)
                if ($text.Contains($temporaryDrive)) {
                    throw "Temporary drive reference remains in $($file.Name)."
                }
                [IO.File]::WriteAllText($file.FullName, $text, $utf8)
            }
        }
        if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) { throw "Solution was not generated: $solution" }
        # Only the game target follows the selected .kproject. Passing OutDir
        # globally would also redirect the shared engine and header tool.
        $gameProject = Join-Path $engineRoot 'Build\Projects\KurisuGame.vcxproj'
        [xml]$gameXml = [IO.File]::ReadAllText($gameProject)
        $namespace = $gameXml.DocumentElement.NamespaceURI
        $defaults = $gameXml.CreateElement('PropertyGroup', $namespace)
        $projectDir = $gameXml.CreateElement('KurisuGameProjectDir', $namespace)
        $projectDir.SetAttribute('Condition', "'`$(KurisuGameProjectDir)' == ''")
        # Without a selected project, KurisuGame is the editor distribution's
        # launcher template and stays in the engine's Build folder. Game
        # packaging always passes KurisuGameProjectDir explicitly.
        $projectDir.InnerText = '$(ProjectDir)..\..'
        [void]$defaults.AppendChild($projectDir)
        $firstOutDir = $gameXml.SelectSingleNode("//*[local-name()='OutDir']")
        [void]$gameXml.DocumentElement.InsertBefore($defaults, $firstOutDir.ParentNode)
        foreach ($node in $gameXml.SelectNodes("//*[local-name()='OutDir']")) {
            $node.InnerText = '$(KurisuGameProjectDir)\Build\Bin\$(Configuration)-windows-x86_64\'
        }
        foreach ($node in $gameXml.SelectNodes("//*[local-name()='IntDir']")) {
            $node.InnerText = '$(KurisuGameProjectDir)\Build\Intermediate\$(Configuration)-windows-x86_64\KurisuGame\'
        }
        $gameXml.Save($gameProject)
        # SLNX BuildDependency is unconditional, even when Premake dependson is
        # filtered. A conditional project reference keeps Debug editor-only.
        $editorProject = Join-Path $engineRoot 'Build\Projects\KurisuEditor.vcxproj'
        [xml]$editorXml = [IO.File]::ReadAllText($editorProject)
        $references = $editorXml.CreateElement('ItemGroup', $namespace)
        $references.SetAttribute('Condition', "'`$(Configuration)' == 'Release'")
        $reference = $editorXml.CreateElement('ProjectReference', $namespace)
        $reference.SetAttribute('Include', 'KurisuGame.vcxproj')
        foreach ($entry in @{
            Project = $gameXml.SelectSingleNode("//*[local-name()='ProjectGuid']").InnerText
            LinkLibraryDependencies = 'false'
            ReferenceOutputAssembly = 'false'
        }.GetEnumerator()) {
            $metadata = $editorXml.CreateElement($entry.Key, $namespace)
            $metadata.InnerText = $entry.Value
            [void]$reference.AppendChild($metadata)
        }
        [void]$references.AppendChild($reference)
        $cppTargets = $editorXml.SelectSingleNode("//*[local-name()='Import' and contains(@Project, 'Microsoft.Cpp.targets')]")
        [void]$editorXml.DocumentElement.InsertBefore($references, $cppTargets)
        $editorXml.Save($editorProject)
    }
    finally { Pop-Location }
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
