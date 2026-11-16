#requires -Version 5.1
# Shared by Package.ps1 and PackageEditor.ps1: finds the redistributable x64
# C++ runtime DLLs that the given binaries need, so packaged games and the
# portable editor run on PCs without the Visual C++ Redistributable.
function Get-VCRuntimeDlls {
    param(
        [Parameter(Mandatory)][string[]]$Binaries,
        [string]$VCToolsInstallDir
    )
    if ([string]::IsNullOrWhiteSpace($VCToolsInstallDir)) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        $vs = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
        if (-not $vs) { throw 'Cannot locate the Visual C++ tools.' }
        $version = (Get-Content -LiteralPath (Join-Path $vs 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
        $VCToolsInstallDir = Join-Path $vs "VC\Tools\MSVC\$version"
    }
    $toolPath = [IO.Path]::GetFullPath($VCToolsInstallDir).TrimEnd('\')
    $toolVersion = Split-Path -Leaf $toolPath
    $vcRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $toolPath))
    $crtRoot = Join-Path $vcRoot "Redist\MSVC\$toolVersion\x64"
    $crt = Get-ChildItem -LiteralPath $crtRoot -Directory | Where-Object { $_.Name -match '^Microsoft\.VC\d+\.CRT$' } | Select-Object -First 1
    if (-not $crt) { throw "Cannot locate the redistributable x64 CRT matching $toolVersion." }
    $crtFiles = @{}
    foreach ($dll in Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll' -File) { $crtFiles[$dll.Name] = $dll }
    $dumpbin = Join-Path $toolPath 'bin\Hostx64\x64\dumpbin.exe'
    if (-not (Test-Path -LiteralPath $dumpbin)) { throw 'Cannot locate dumpbin for runtime dependency inspection.' }
    $pending = New-Object 'System.Collections.Generic.Queue[string]'
    foreach ($binary in $Binaries) { $pending.Enqueue($binary) }
    $required = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    while ($pending.Count -gt 0) {
        $dependencies = & $dumpbin /NOLOGO /DEPENDENTS $pending.Dequeue()
        if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect binary dependencies.' }
        foreach ($line in $dependencies) {
            if ($line -match '^\s+([\w.-]+\.dll)\s*$' -and $crtFiles.ContainsKey($Matches[1])) {
                $name = $Matches[1]
                if ($required.Add($name)) { $pending.Enqueue($crtFiles[$name].FullName) }
            }
        }
    }
    $runtimeDlls = @($required | ForEach-Object { $crtFiles[$_] })
    if ($runtimeDlls.Count -eq 0) { throw 'No redistributable CRT dependencies found.' }
    return $runtimeDlls
}
