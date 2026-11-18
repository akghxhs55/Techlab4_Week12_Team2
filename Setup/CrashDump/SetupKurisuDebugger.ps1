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

    $installationPath = & $vsWhere -latest -version '[18.0,19.0)' `
        -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installationPath) {
        throw 'Visual Studio 2026 was not found.'
    }

    $registryEditor = Join-Path $installationPath 'Common7\IDE\VsRegEdit.exe'
    if (-not (Test-Path -LiteralPath $registryEditor -PathType Leaf)) {
        throw "Visual Studio's VsRegEdit.exe was not found: $registryEditor"
    }

    # Keep the shared .vssettings file as the source of the four values.
    # VsRegEdit updates the selected installation's private user registry.
    [xml]$settings = [IO.File]::ReadAllText($settingsFile)
    $valueTypes = [ordered]@{
        UseSourceLink = 'dword'
        SymbolPath = 'string'
        SymbolPathState = 'string'
        SymbolCacheDir = 'string'
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

    foreach ($name in $valueTypes.Keys) {
        & $registryEditor set $installationPath HKCU Debugger `
            $name $valueTypes[$name] $values[$name]
        if ($LASTEXITCODE -ne 0) {
            throw "Could not apply debugger setting '$name': $LASTEXITCODE"
        }
    }

    Write-Host 'Kurisu debugger settings applied without opening Visual Studio.'
    Write-Host 'Restart any running Visual Studio instances to use the settings.'
    exit 0
}
catch {
    [Console]::Error.WriteLine(
        "Debugger setup failed: $($_.Exception.Message)")
    exit 1
}
