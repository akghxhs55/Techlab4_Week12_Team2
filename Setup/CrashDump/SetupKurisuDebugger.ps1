#requires -Version 5.1
$ErrorActionPreference = 'Stop'

try {
    $settingsFile = Join-Path $PSScriptRoot 'KurisuDebugging.vssettings'
    $vsWhere = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'

    if (-not (Test-Path -LiteralPath $settingsFile -PathType Leaf)) {
        throw "Settings file was not found: $settingsFile"
    }
    if (-not (Test-Path -LiteralPath $vsWhere -PathType Leaf)) {
        throw 'Visual Studio Installer''s vswhere.exe was not found.'
    }

    # Configure every installed IDE, including the Insiders channel.
    # Match vswhere's UTF-8 output when installation paths contain Unicode.
    $originalEncoding = [Console]::OutputEncoding
    try {
        [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
        $installationPaths = @(& $vsWhere -prerelease -version '[18.0,19.0)' `
            -property installationPath -utf8)
        $locatorExitCode = $LASTEXITCODE
    }
    finally {
        [Console]::OutputEncoding = $originalEncoding
    }
    $installationPaths = @($installationPaths |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Select-Object -Unique)
    if ($locatorExitCode -ne 0 -or $installationPaths.Count -eq 0) {
        throw 'Visual Studio 2026 was not found.'
    }

    foreach ($installationPath in $installationPaths) {
        $registryEditor = Join-Path $installationPath 'Common7\IDE\VsRegEdit.exe'
        if (-not (Test-Path -LiteralPath $registryEditor -PathType Leaf)) {
            throw "Visual Studio's VsRegEdit.exe was not found: $registryEditor"
        }
    }

    # Keep the shared .vssettings file as the source of the debugger values.
    # VsRegEdit updates the selected installation's private user registry.
    [xml]$settings = [IO.File]::ReadAllText($settingsFile)
    $valueTypes = [ordered]@{
        UseSourceLink = 'dword'
        SymbolPath = 'string'
        SymbolPathState = 'string'
        SymbolCacheDir = 'string'
        SymbolUseMSSymbolServers = 'dword'
        AutoLoadFromSymbolPath = 'dword'
        SymbolsAlwaysLoadAdjacent = 'dword'
        SymbolUseExcludeList = 'dword'
        SymbolIncludeList = 'string'
        SymbolIncludeListState = 'string'
    }
    $values = @{}
    foreach ($name in $valueTypes.Keys) {
        $property = $settings.SelectSingleNode(
            "/UserSettings/Category[@name='Debugger']/PropertyValue[@name='$name']")
        if ($null -eq $property -or
            [string]::IsNullOrWhiteSpace($property.InnerText)) {
            throw "Missing debugger setting: $name"
        }
        $values[$name] = $property.InnerText
        if ($valueTypes[$name] -eq 'dword') {
            $values[$name] = ([uint32]$property.InnerText).ToString()
        }
    }

    $includedModules = @($values['SymbolIncludeList'] -split ';')
    $includedStates = @($values['SymbolIncludeListState'] -split ';')
    if ($includedModules.Count -ne $includedStates.Count -or
        @($includedStates | Where-Object { $_ -notin @('0', '1') }).Count -gt 0) {
        throw 'Invalid SymbolIncludeList or SymbolIncludeListState.'
    }

    $cacheDirectory = [Environment]::ExpandEnvironmentVariables(
        $values['SymbolCacheDir'])
    if (-not [IO.Path]::IsPathRooted($cacheDirectory) -or
        $cacheDirectory.IndexOfAny([char[]]'*;%') -ge 0) {
        throw "Invalid symbol cache directory: $cacheDirectory"
    }

    $stores = @($values['SymbolPath'] -split ';')
    $states = @($values['SymbolPathState'] -split ';')
    if ($stores.Count -ne $states.Count) {
        throw 'SymbolPath and SymbolPathState have different entry counts.'
    }
    $serverPaths = @()
    for ($index = 0; $index -lt $stores.Count; ++$index) {
        if ($states[$index] -ne '1') {
            continue
        }
        $store = $stores[$index].Trim()
        if (-not $store.StartsWith('\\') -or $store.Contains('*')) {
            throw "Expected a UNC symbol store path: $store"
        }
        if (-not (Test-Path -LiteralPath $store -PathType Container)) {
            throw "Cannot read symbol store '$store'. Check network access and share permissions."
        }
        $hasMarker = Test-Path -LiteralPath (Join-Path $store 'pingme.txt') `
            -PathType Leaf
        $hasIndex = Test-Path -LiteralPath (Join-Path $store '000admin') `
            -PathType Container
        if (-not $hasMarker -and -not $hasIndex) {
            throw "No SymStore index in '$store'. Publish the original build with PublishKurisuSymbols.bat."
        }
        # SymSrv retrieves both EXE/DLL images and PDBs by their build IDs.
        # A local downstream cache keeps clients from writing into the share.
        $serverPaths += "srv*$cacheDirectory*$store"
    }
    if ($serverPaths.Count -eq 0) {
        throw 'No enabled symbol store was configured.'
    }
    [void][IO.Directory]::CreateDirectory($cacheDirectory)
    $values['SymbolCacheDir'] = $cacheDirectory
    $values['SymbolPath'] = $serverPaths -join ';'
    $values['SymbolPathState'] = (@('1') * $serverPaths.Count) -join ';'

    foreach ($installationPath in $installationPaths) {
        $registryEditor = Join-Path $installationPath 'Common7\IDE\VsRegEdit.exe'
        foreach ($name in $valueTypes.Keys) {
            & $registryEditor set $installationPath HKCU Debugger `
                $name $valueTypes[$name] $values[$name]
            if ($LASTEXITCODE -ne 0) {
                throw "Could not apply '$name' to '$installationPath': $LASTEXITCODE"
            }
        }
        Write-Host "Debugger settings: $installationPath"
    }

    # Visual Studio also reads this standard path when a new IDE starts.
    # Keep existing user paths rather than replacing other symbol stores.
    if ($values['SymbolUseMSSymbolServers'] -eq '1') {
        $serverPaths += "srv*$cacheDirectory*https://msdl.microsoft.com/download/symbols"
    }
    $existingPath = [Environment]::GetEnvironmentVariable('_NT_SYMBOL_PATH', 'User')
    if ([string]::IsNullOrWhiteSpace($existingPath)) {
        $existingPath = [Environment]::GetEnvironmentVariable(
            '_NT_SYMBOL_PATH', 'Machine')
    }
    $searchPaths = @($serverPaths)
    foreach ($entry in @($existingPath -split ';')) {
        if (-not [string]::IsNullOrWhiteSpace($entry) -and
            $searchPaths -notcontains $entry) {
            $searchPaths += $entry
        }
    }
    $symbolSearchPath = $searchPaths -join ';'
    [Environment]::SetEnvironmentVariable(
        '_NT_SYMBOL_PATH', $symbolSearchPath, 'User')
    [Environment]::SetEnvironmentVariable(
        '_NT_SYMBOL_PATH', $symbolSearchPath, 'Process')

    Write-Host 'Kurisu debugger settings applied without opening Visual Studio.'
    Write-Host "Automatic symbol search: $($values['AutoLoadFromSymbolPath'])"
    Write-Host "Included modules: $($values['SymbolIncludeList'])"
    Write-Host "Symbol search path: $symbolSearchPath"
    Write-Host 'OpenKurisuDump verifies matching shared PDBs before starting Visual Studio.'
    Write-Host 'Restart any running Visual Studio instances to use the settings.'
    exit 0
}
catch {
    [Console]::Error.WriteLine(
        "Debugger setup failed: $($_.Exception.Message)")
    exit 1
}
