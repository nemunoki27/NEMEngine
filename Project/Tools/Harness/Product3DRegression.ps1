param([string]$Probe, [switch]$RunOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$root = Join-Path $repo 'LocalWork/Verification/Product3D'
$game = Join-Path $root 'Sandbox'
$assets = Join-Path $game 'GameAssets'
$source = Join-Path $repo 'Project/Sandbox'
$descriptor = Join-Path $game 'Sandbox.nemproject'
$manifest = Join-Path $root 'Inputs.json'
$exe = Join-Path $root 'Products/Product3D/Product3D.exe'
$sceneID = '4e454d50524f44334400000000000001'
$scriptID = '4e454d50524f44334400000000000002'
$typeID = '4e454d50-524f-4433-4400-000000000003'
function Write-TestJson($path,$value) {
    [IO.File]::WriteAllText($path, ($value | ConvertTo-Json -Depth 60), [Text.UTF8Encoding]::new($false))
}
if (!$RunOnly) {
    foreach ($folder in 'GameAssets/Scenes','GameAssets/Models','Scripts','ProjectSettings/Runtime','Packages') {
        [void][IO.Directory]::CreateDirectory((Join-Path $game $folder))
    }
    $projectID = '4e454d50524f44334400000000000000'
    Write-TestJson $descriptor @{schemaVersion=1;projectGuid=$projectID;name='Product3D';sceneStorage='Monolithic';assetsDirectory='GameAssets';packagesDirectory='Packages';projectSettingsDirectory='ProjectSettings'}
    Copy-Item -LiteralPath (Join-Path $source 'Sandbox.vcxproj') -Destination $game -Force
    foreach ($file in 'manifest.json','packages-lock.json') { Copy-Item (Join-Path $source "Packages/$file") (Join-Path $game 'Packages') -Force }
    foreach ($file in 'RobotExpressive.glb','RobotExpressive.glb.meta') {
        Copy-Item (Join-Path $source "GameAssets/Models/RobotExpressive/$file") (Join-Path $assets 'Models') -Force
    }
    Copy-Item (Join-Path $repo 'Project/Tests/Fixtures/Product3D/PhysicsAndSkin.cs') $assets -Force
    Write-TestJson (Join-Path $assets 'PhysicsAndSkin.cs.meta') @{schemaVersion=2;guid=$scriptID;type='Script';importer='ScriptImporter';importerVersion=1;settings=@{};scripts=@(@{fullTypeName='PhysicsAndSkin';scriptTypeId=$typeID;fields=@()})}
    $robotID='0000000000000010'
    $meshMeta=Get-Content (Join-Path $assets 'Models/RobotExpressive.glb.meta') -Raw -Encoding UTF8|ConvertFrom-Json
    $components=@{
        Name=@{name='RobotExpressive'};MeshRenderer=@{mesh=$meshMeta.guid;visible=$true}
        SkinnedAnimation=@{clip='Idle';enabled=$true;loop=$true;playbackSpeed=1;transitionDuration=0.1}
        Transform=@{dimension=1;localPos=@{x=0;y=0;z=4};localRotation=@{x=0;y=0;z=0;w=1};localScale=@{x=1;y=1;z=1}}
        Hierarchy=@{parentLocalFileID='';siblingOrder=0}
        SceneObject=@{localFileId=$robotID;activeSelf=$true;tag='Untagged';visibilityLayerMask=4294967295}
        Script=@(@{enabled=$true;lastKnownTypeName='PhysicsAndSkin';scriptAsset=$scriptID;scriptTypeId=$typeID;scriptSlotId='0000000000000100';serializedFields=@{fields=@{}}})
    }
    $camera=(Get-Content (Join-Path $repo 'Templates/ManagedProject/3D/GameAssets/Scenes/Main.scene.json') -Raw -Encoding UTF8|ConvertFrom-Json).Entities|Where-Object {$_.Components.PSObject.Properties.Name -contains 'PerspectiveCamera'}|Select-Object -First 1
    $world=@{SchemaVersion=3;Header=@{name='PhysicsAndSkin';subScenes=@()};Entities=@(@{LocalFileID=$robotID;Components=$components});PrefabInstances=@()}
    if ($camera) {$world.Entities+= $camera}
    Write-TestJson (Join-Path $assets 'Scenes/Physics.scene.json') $world
    Write-TestJson (Join-Path $assets 'Scenes/Physics.scene.json.meta') @{schemaVersion=2;guid=$sceneID;type='Scene';importer='SceneImporter';importerVersion=1;settings=@{}}
    Write-TestJson (Join-Path $game 'ProjectSettings/Runtime/StartupScene.json') @{activeScene=$sceneID}
    $core = Join-Path $repo 'Generated/Output/Release/Sandbox/Managed/NEM.ScriptCore.dll'
    $generator = Join-Path $repo 'Project/Engine/Managed/NEM.ScriptCodeGen/bin/Release/netstandard2.0/NEM.ScriptCodeGen.dll'
    @"
<Project Sdk="Microsoft.NET.Sdk">
<PropertyGroup><TargetFramework>net10.0</TargetFramework><Nullable>enable</Nullable><ImplicitUsings>enable</ImplicitUsings><OutputPath>../Managed/Release/</OutputPath><AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath></PropertyGroup>
<ItemGroup><Reference Include="NEM.ScriptCore" HintPath="$core"/><Analyzer Include="$generator"/><Compile Include="../GameAssets/*.cs"/><AdditionalFiles Include="../GameAssets/*.cs.meta"/></ItemGroup>
</Project>
"@ | Set-Content (Join-Path $game 'Scripts/GameScripts.csproj') -Encoding utf8
    if (!$Probe) {$Probe=Join-Path $repo 'Generated/Output/Debug/GameBuildServiceProbe/GameBuildServiceProbe.exe'}
    $env:NEMENGINE_ROOT=Join-Path $repo 'Project'
    $env:NEMENGINE_PROJECT=$descriptor
    & $Probe $game $sceneID $manifest --manifest
    if ($LASTEXITCODE -ne 0) {throw 'Product input collection failed'}
    $m=Get-Content $manifest -Raw -Encoding UTF8|ConvertFrom-Json
    $m.projectPath=$descriptor
    $m.prebuiltRuntime=$true
    $m.buildToolProject=''
    $m.productName='Product3D'
    $m.executableName='Product3D.exe'
    $m.gameWidth=640
    $m.gameHeight=360
    Write-TestJson $manifest $m
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'Tools/BuildGame.ps1') -ManifestPath $manifest
    if ($LASTEXITCODE -ne 0) {throw '3D product build failed'}
}
$report=Join-Path $root 'result.log'
[IO.File]::WriteAllText($report,'')
$psi=[Diagnostics.ProcessStartInfo]::new($exe)
$psi.WorkingDirectory=$root
$psi.UseShellExecute=$false
$psi.WindowStyle='Hidden'
$psi.EnvironmentVariables['LOCALAPPDATA']=Join-Path $root 'User'
$psi.EnvironmentVariables['NEM_PRODUCT_3D_REPORT']=$report
$p=[Diagnostics.Process]::Start($psi)
try {
    if (!$p.WaitForExit(90000)) {$p.Kill(); $p.WaitForExit(); throw '3D game timed out'}
    $lines=[IO.File]::ReadAllLines($report)
    if ($p.ExitCode -ne 0 -or $lines -notcontains 'PASS' -or ($lines -match '^FAIL')) {throw "3D gameplay failed: $lines"}
    $lines
} finally {$p.Dispose()}
