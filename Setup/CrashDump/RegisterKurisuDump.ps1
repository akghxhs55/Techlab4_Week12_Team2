#requires -Version 5.1
[CmdletBinding(SupportsShouldProcess)]
param()

$ErrorActionPreference = 'Stop'

function Set-UserRegistryString {
    param(
        [string]$SubKey,
        [AllowEmptyString()][string]$Name,
        [AllowEmptyString()][string]$Value
    )

    $registryKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($SubKey)
    try {
        $registryKey.SetValue($Name, $Value, [Microsoft.Win32.RegistryValueKind]::String)
    }
    finally {
        $registryKey.Dispose()
    }
}

try {
    foreach ($fileName in @(
        'OpenKurisuDump.ps1',
        'SetupKurisuDebugger.ps1',
        'KurisuDebugging.vssettings',
        'TestKurisuDumpSymbols.ps1',
        'CrashDumpSymbols.cs'
    )) {
        $requiredFile = Join-Path $PSScriptRoot $fileName
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
            throw "Required file was not found: $requiredFile"
        }
    }

    $applicationName = 'Kurisu Dump Debugger'
    $progId = 'Kurisu.CrashDump.1'
    $progIdKey = "Software\Classes\$progId"
    $capabilitiesKey = 'Software\Kurisu\DumpDebugger\Capabilities'
    $launcherScript = Join-Path $PSScriptRoot 'OpenKurisuDump.ps1'
    $powershell = Join-Path $PSHOME 'powershell.exe'
    if (-not (Test-Path -LiteralPath $powershell -PathType Leaf)) {
        throw 'Run registration with Windows PowerShell 5.1.'
    }

    # Use -File rather than -Command so dump paths are passed as data.
    # Pause only on errors; a successful launch closes the console automatically.
    $openCommand = '"{0}" -NoProfile -ExecutionPolicy Bypass ' +
        '-File "{1}" -PauseOnError -DumpPath "%1"'
    $openCommand = $openCommand -f $powershell, $launcherScript
    Write-Verbose "Open command: $openCommand"

    if (-not $PSCmdlet.ShouldProcess(
        'Current Windows user', 'Register Kurisu Dump Debugger and open Default Apps')) {
        exit 0
    }

    Set-UserRegistryString $progIdKey '' 'Kurisu crash dump'
    Set-UserRegistryString "$progIdKey\Application" `
        'ApplicationName' $applicationName
    Set-UserRegistryString "$progIdKey\shell" '' 'open'
    Set-UserRegistryString "$progIdKey\shell\open" '' "Open with $applicationName"
    Set-UserRegistryString "$progIdKey\shell\open\command" '' $openCommand

    # Offer this handler in Open With without replacing the user's default.
    Set-UserRegistryString 'Software\Classes\.dmp\OpenWithProgids' $progId ''
    Set-UserRegistryString $capabilitiesKey 'ApplicationName' $applicationName
    Set-UserRegistryString $capabilitiesKey 'ApplicationDescription' `
        'Apply Kurisu debugger settings and open crash dumps in Visual Studio.'
    Set-UserRegistryString "$capabilitiesKey\FileAssociations" '.dmp' $progId
    Set-UserRegistryString 'Software\RegisteredApplications' `
        $applicationName $capabilitiesKey

    # Tell Explorer that file associations have changed.
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class KurisuDumpAssociation
{
    [DllImport("shell32.dll")]
    public static extern void SHChangeNotify(
        uint eventId, uint flags, IntPtr item1, IntPtr item2);
}
'@
    [KurisuDumpAssociation]::SHChangeNotify(
        0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)

    Write-Host "Registered for the current user: $applicationName"
    Write-Host 'ONE-TIME STEP: Set the default app for .dmp to Kurisu Dump Debugger.'
    Write-Host 'Afterward, double-click a .dmp file to apply settings and open Visual Studio.'
    Write-Host 'Keep the CrashDump folder in place. If it moves, run this registration again.'

    # Windows requires the user to choose defaults through its system UI.
    # Never write to the protected Explorer FileExts\UserChoice key.
    $settingsUri = 'ms-settings:defaultapps?registeredAppUser=' +
        [Uri]::EscapeDataString($applicationName)
    try {
        Start-Process -FilePath $settingsUri
    }
    catch {
        Write-Warning 'Open Windows Settings > Apps > Default apps and search for .dmp.'
    }
    exit 0
}
catch {
    [Console]::Error.WriteLine("Dump handler registration failed: $($_.Exception.Message)")
    exit 1
}
