#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$Executable = 'DefaultProject\Build\Package\DefaultProject\HitoriGame.exe',
    [string]$Project = 'DefaultProject\Build\Package\DefaultProject\DefaultProject.hproject',
    [int]$TimeoutSeconds = 45
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = (Resolve-Path -LiteralPath (Join-Path $root $Executable)).ProviderPath
$projectFile = (Resolve-Path -LiteralPath (Join-Path $root $Project)).ProviderPath
$reportRoot = Join-Path $root 'Build\Tests\Smoke'
New-Item -ItemType Directory -Path $reportRoot -Force | Out-Null
$report = Join-Path $reportRoot ([IO.Path]::GetFileNameWithoutExtension($exe) + '-load.txt')
if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report }
# A hidden test window has no Process.MainWindowHandle. Close only this test PID.
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class HitoriSmokeWindows {
    private delegate bool Callback(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] private static extern bool EnumWindows(Callback callback, IntPtr data);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    public static void Close(uint processId) {
        EnumWindows((window, data) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner == processId) PostMessage(window, 0x0010, IntPtr.Zero, IntPtr.Zero);
            return true;
        }, IntPtr.Zero);
    }
}
'@
$beforeProfile = $env:HITORI_LOAD_PROFILE
$testProcess = $null
try {
    $env:HITORI_LOAD_PROFILE = $report
    $testProcess = Start-Process -FilePath $exe -ArgumentList ('"' + $projectFile + '"') -WorkingDirectory $reportRoot -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not $testProcess.HasExited -and -not (Test-Path -LiteralPath $report) -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 250
        $testProcess.Refresh()
    }
    if (-not (Test-Path -LiteralPath $report)) { throw 'Engine/project initialization did not complete.' }
    Start-Sleep -Seconds 2
    $testProcess.Refresh()
    if ($testProcess.HasExited) { throw "Application exited unexpectedly: $($testProcess.ExitCode)" }
    [HitoriSmokeWindows]::Close([uint32]$testProcess.Id)
    if (-not $testProcess.WaitForExit(10000)) { throw 'Application did not shut down after WM_CLOSE.' }
    if ($testProcess.ExitCode -ne 0) { throw "Application shutdown failed: $($testProcess.ExitCode)" }
    Write-Host "Smoke passed: $([IO.Path]::GetFileName($exe)); initialization, 2-second run, clean shutdown; different working directory."
} finally {
    if ($testProcess -and -not $testProcess.HasExited) { Stop-Process -Id $testProcess.Id }
    $env:HITORI_LOAD_PROFILE = $beforeProfile
}
