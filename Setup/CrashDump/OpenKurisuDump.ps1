#requires -Version 5.1
param(
    [Parameter(Mandatory)][string]$DumpPath,
    [switch]$PauseOnError
)

$ErrorActionPreference = 'Stop'

try {
    if (-not (Test-Path -LiteralPath $DumpPath -PathType Leaf)) {
        throw "Dump file was not found: $DumpPath"
    }
    $dumpFile = Get-Item -LiteralPath $DumpPath
    if ($dumpFile.Extension -ine '.dmp') {
        throw 'Select a .dmp file.'
    }

    $setupScript = Join-Path $PSScriptRoot 'SetupKurisuDebugger.ps1'
    $validationScript = Join-Path $PSScriptRoot 'TestKurisuDumpSymbols.ps1'
    $vsWhere = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $setupScript -PathType Leaf)) {
        throw "Debugger setup script was not found: $setupScript"
    }
    foreach ($requiredFile in @(
        $validationScript, (Join-Path $PSScriptRoot 'CrashDumpSymbols.cs')
    )) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
            throw "Symbol validation file was not found: $requiredFile"
        }
    }
    if (-not (Test-Path -LiteralPath $vsWhere -PathType Leaf)) {
        throw 'Visual Studio Installer''s vswhere.exe was not found.'
    }

    # Select an IDE covered by SetupKurisuDebugger, including Insiders.
    $originalEncoding = [Console]::OutputEncoding
    try {
        [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
        $installationPath = & $vsWhere -latest -prerelease `
            -version '[18.0,19.0)' -property installationPath -utf8
        $locatorExitCode = $LASTEXITCODE
    }
    finally {
        [Console]::OutputEncoding = $originalEncoding
    }
    if ($locatorExitCode -ne 0 -or
        [string]::IsNullOrWhiteSpace($installationPath)) {
        throw 'Visual Studio 2026 was not found.'
    }
    $devenv = Join-Path $installationPath 'Common7\IDE\devenv.exe'
    if (-not (Test-Path -LiteralPath $devenv -PathType Leaf)) {
        throw "Visual Studio's devenv.exe was not found: $devenv"
    }

    # Setup exits explicitly, so run it in a separate PowerShell process.
    & (Join-Path $PSHOME 'powershell.exe') -NoProfile `
        -ExecutionPolicy Bypass -File $setupScript
    if ($LASTEXITCODE -ne 0) {
        throw "Debugger setup failed with exit code $LASTEXITCODE."
    }

    # Check the dump's own GUID/Age against the shared PDB, not a local build.
    & (Join-Path $PSHOME 'powershell.exe') -NoProfile `
        -ExecutionPolicy Bypass -File $validationScript -DumpPath $dumpFile.FullName
    if ($LASTEXITCODE -ne 0) {
        throw 'Shared PDB validation failed. See the required identity and search paths above.'
    }

    # The child's process environment cannot update this launcher's environment.
    # Reload the persisted symbol path before starting the new IDE.
    $env:_NT_SYMBOL_PATH = [Environment]::GetEnvironmentVariable(
        '_NT_SYMBOL_PATH', 'User')

    # A plain file argument opens a new IDE instance with the applied settings.
    # Do not use /Edit, which can reuse an IDE with stale debugger settings.
    Start-Process -FilePath $devenv `
        -ArgumentList ('"{0}"' -f $dumpFile.FullName) `
        -WorkingDirectory $dumpFile.DirectoryName

    Write-Host "Opened dump in Visual Studio: $($dumpFile.FullName)"
    Write-Host 'Select "Debug with Native Only" in the dump summary to inspect the crash.'
    exit 0
}
catch {
    [Console]::Error.WriteLine("Could not open dump: $($_.Exception.Message)")
    if ($PauseOnError) {
        [void](Read-Host 'Press Enter to close')
    }
    exit 1
}
