param([string]$MSBuild = '')
$ErrorActionPreference = 'Stop'
$repoDir = Split-Path -Parent $PSScriptRoot
$sourceProject = Join-Path $repoDir 'Build/Projects/HitoriEditor.vcxproj'
$testProject = Join-Path $repoDir 'Build/Projects/EditorUXSmoke.vcxproj'
if (!(Test-Path -LiteralPath $sourceProject)) { throw 'Run SlnGenerator.bat and build HitoriEditor Debug first.' }
if (!$MSBuild) {
    $found = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($found) { $MSBuild = $found.Source }
    else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $MSBuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    }
}
if (!$MSBuild) { throw 'MSBuild was not found. Pass -MSBuild with the full executable path.' }
$projectText = [IO.File]::ReadAllText($sourceProject)
$projectText = [regex]::Replace($projectText, '<ClCompile Include="[^"]+"(?:\s*/>|>[\s\S]*?</ClCompile>)', '')
$projectText = [regex]::Replace($projectText, '<ResourceCompile Include="[^"]+"\s*/>', '')
$projectText = [regex]::Replace($projectText, '<PreBuildEvent>[\s\S]*?</PreBuildEvent>', '')
$projectText = [regex]::Replace($projectText, '<PostBuildEvent>[\s\S]*?</PostBuildEvent>', '')
$projectText = $projectText.Replace('<TargetName>HitoriEditor</TargetName>', '<TargetName>EditorUXSmoke</TargetName>')
$projectText = $projectText.Replace('<SubSystem>Windows</SubSystem>', '<SubSystem>Console</SubSystem>')
$projectText = $projectText.Replace('<PrecompiledHeader>Use</PrecompiledHeader>', '<PrecompiledHeader>NotUsing</PrecompiledHeader>')
$projectText = $projectText.Replace('<DebugInformationFormat>EditAndContinue</DebugInformationFormat>', '<DebugInformationFormat>OldStyle</DebugInformationFormat>')
$projectText = $projectText.Replace('<GenerateDebugInformation>true</GenerateDebugInformation>', '<GenerateDebugInformation>false</GenerateDebugInformation>')
$projectText = $projectText.Replace('<LinkIncremental>true</LinkIncremental>', '<LinkIncremental>false</LinkIncremental>')
$projectText = $projectText.Replace('HitoriEditor\</IntDir>', 'EditorUXSmoke\</IntDir>')
$objects = 'EditorPCH', 'EditorViewportClient', 'Gizmo', 'GizmoRenderer', 'OutlinerPanel', 'DetailsPanel'
$objectPaths = ($objects | ForEach-Object { '..\Intermediate\Debug-windows-x86_64\HitoriEditor\' + $_ + '.obj' }) -join ';'
$projectText = $projectText.Replace('<AdditionalDependencies>', '<AdditionalDependencies>' + $objectPaths + ';')
$projectText = $projectText.Replace('<Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />', '<ItemGroup><ClCompile Include="..\..\Tests\EditorUXSmoke.cpp" /></ItemGroup><Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />')
[IO.File]::WriteAllText($testProject, $projectText)
Push-Location $repoDir
try {
    & $MSBuild $testProject /p:Configuration=Debug /p:Platform=x64 /p:BuildProjectReferences=false /m:1 /nr:false /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw 'Editor UX smoke test build failed.' }
    & (Join-Path $repoDir 'Build/Bin/Debug-windows-x86_64/EditorUXSmoke.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Editor UX smoke test failed.' }
}
finally { Pop-Location }
