#requires -Version 5.1
param(
    [Parameter(Mandatory)][string]$DumpPath,
    [string]$SettingsPath,
    [string[]]$Store,
    [string]$CacheDirectory
)

$ErrorActionPreference = 'Stop'

try {
    if (-not $SettingsPath) {
        $SettingsPath = Join-Path $PSScriptRoot 'KurisuDebugging.vssettings'
    }
    [xml]$settings = [IO.File]::ReadAllText($SettingsPath)
    $debugger = $settings.UserSettings.Category |
        Where-Object { $_.name -eq 'Debugger' }
    $properties = @{}
    foreach ($property in $debugger.PropertyValue) {
        $properties[$property.name] = $property.InnerText
    }
    if (-not $Store) {
        $paths = @($properties['SymbolPath'] -split ';')
        $states = @($properties['SymbolPathState'] -split ';')
        if ($paths.Count -ne $states.Count) {
            throw 'SymbolPath and SymbolPathState have different entry counts.'
        }
        $Store = @(for ($index = 0; $index -lt $paths.Count; ++$index) {
            if ($states[$index] -eq '1') { $paths[$index].Trim() }
        })
    }
    if (-not $Store) { throw 'No enabled symbol store was configured.' }
    if (-not $CacheDirectory) { $CacheDirectory = $properties['SymbolCacheDir'] }
    $CacheDirectory = [Environment]::ExpandEnvironmentVariables($CacheDirectory)
    if (-not [IO.Path]::IsPathRooted($CacheDirectory) -or
        $CacheDirectory.IndexOfAny([char[]]'*;%') -ge 0) {
        throw "Invalid symbol cache directory: $CacheDirectory"
    }

    Add-Type -Path (Join-Path $PSScriptRoot 'CrashDumpSymbols.cs')
    $identities = [Kurisu.Debugging.CrashDumpSymbols]::ReadDump($DumpPath)
    foreach ($identity in $identities) {
        Write-Host "Module: $($identity.ModuleName)"
        Write-Host "Required PDB: $($identity.PdbName)"
        Write-Host "Required GUID: $($identity.Guid), Age: $($identity.Age)"
        Write-Host "SymStore key: $($identity.StoreKey)"
        $relativePath = Join-Path $identity.PdbName `
            (Join-Path $identity.StoreKey $identity.PdbName)
        $cachedPdb = Join-Path $CacheDirectory $relativePath
        $validated = $false
        $failures = @()
        foreach ($symbolStore in $Store) {
            $sharedPdb = Join-Path $symbolStore $relativePath
            Write-Host "Checking shared PDB: $sharedPdb"
            try {
                [Kurisu.Debugging.CrashDumpSymbols]::VerifyAndCache(
                    $sharedPdb, $cachedPdb, $identity)
                Write-Host 'Shared PDB GUID/Age: MATCH'
                Write-Host "Verified cache file: $cachedPdb"
                $validated = $true
                break
            }
            catch {
                $exception = $_.Exception
                if ($exception -is [Management.Automation.MethodInvocationException] -and
                    $exception.InnerException) {
                    $exception = $exception.InnerException
                }
                $reason = $exception.Message
                $failures += "$sharedPdb`r`n  $reason"
            }
        }
        if (-not $validated) {
            throw ("Could not verify and cache the shared PDB for $($identity.ModuleName).`r`n" +
                ($failures -join "`r`n"))
        }
    }
    Write-Host 'Shared PDB identity validation and cache preparation succeeded.'
    exit 0
}
catch {
    [Console]::Error.WriteLine("Shared PDB validation failed: $($_.Exception.GetBaseException().Message)")
    exit 1
}
