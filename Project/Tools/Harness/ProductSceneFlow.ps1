param([string]$Probe = "", [switch]$PrepareOnly, [switch]$RunOnly, [switch]$ComponentCheck)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$root = Join-Path $repo 'LocalWork/Verification/ProductFlow'
$game = Join-Path $root 'Sandbox'
$assets = Join-Path $game 'GameAssets'
$source = Join-Path $repo 'Project/Tests/Fixtures/ProductSceneFlow'
$manifestPath = Join-Path $root 'Inputs.json'
$exe = Join-Path $root 'Products/SceneFlow/SceneFlow.exe'
$encoding = [Text.UTF8Encoding]::new($false)
function Write-FlowJson([string]$path, $data) {
    [IO.File]::WriteAllText($path, ($data | ConvertTo-Json -Depth 40), $encoding)
}
function New-FlowMeta([string]$type, [string]$id) {
    return @{schemaVersion=2; guid=$id; type=$type; importer=$type+'Importer'; importerVersion=1; settings=@{}}
}
function New-FlowObject([string]$name, [string]$id, [float]$x=0, [float]$y=0) {
    return @{ LocalFileID=$id; Components=@{
        Name=@{name=$name}; SceneObject=@{localFileId=$id;activeSelf=$true;tag='Untagged';visibilityLayerMask=4294967295}
        Hierarchy=@{parentLocalFileID='';siblingOrder=0}
        Transform=@{dimension=0;localPos=@{x=$x;y=$y;z=0};localRotation=@{x=0;y=0;z=0;w=1};localScale=@{x=1;y=1;z=1}}
    }}
}
function New-FlowText([string]$text,[float]$size) {
    return @{ dimension=0; text=$text; font='c4f158fab92cfcf265eaa1407b4aa1a4'; fontSize=$size; pivot=@{x=0.5;y=0.5} }
}
if (!$RunOnly) {
    foreach ($relative in 'GameAssets/Scenes','GameAssets/Fonts','GameAssets/RuntimeOnly','Scripts','ProjectSettings/Runtime','Packages') {
        New-Item -ItemType Directory -Path (Join-Path $game $relative) -Force | Out-Null
    }
    $names = 'Title','Select','Game','Clear','Result','NextStage'
    $guid = @{}
    for ($i=0; $i -lt $names.Count; ++$i) { $guid[$names[$i]] = '4e454d50524f44464c4f5700000000' + $i.ToString('00') }
    $overlayGuid = '4e454d50524f44464c4f5700000000ff'
    $projectID = '4e454d50524f44464c4f570000001000'
    $descriptor = Join-Path $game 'Sandbox.nemproject'
    Write-FlowJson $descriptor @{schemaVersion=1;projectGuid=$projectID;name='SceneFlow';sceneStorage='Monolithic';assetsDirectory='GameAssets';packagesDirectory='Packages';projectSettingsDirectory='ProjectSettings'}
    Copy-Item -LiteralPath (Join-Path $repo 'Project/Sandbox/Sandbox.vcxproj') -Destination $game -Force
    foreach ($name in 'manifest.json','packages-lock.json') {
        Copy-Item -LiteralPath (Join-Path $repo "Project/Sandbox/Packages/$name") -Destination (Join-Path $game 'Packages') -Force
    }
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $repo 'Project/Sandbox/GameAssets/Fonts') -Filter '*_msdf*' -File) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $assets 'Fonts') -Force
    }
    Copy-Item -LiteralPath (Join-Path $source 'SceneFlow.cs') -Destination $assets -Force
    if (Test-Path -LiteralPath (Join-Path $source 'ProductInteractions.cs')) {
        Copy-Item -LiteralPath (Join-Path $source 'ProductInteractions.cs') -Destination $assets -Force
    }
    if (Test-Path -LiteralPath (Join-Path $source 'ProductComponents.cs')) {
        Copy-Item -LiteralPath (Join-Path $source 'ProductComponents.cs') -Destination $assets -Force
    }

    $scriptID='4e454d50524f44464c4f570000000100'
    $typeID='4e454d50-524f-4446-4c4f-570000000101'
    $prefab='4e454d50524f44464c4f570000000200'
    $media = @(
        @{field='motion';name='Motion.animClip';type='AnimationClip';guid='4e454d50524f44464c4f570000000301'}
        @{field='sequence';name='Motion.timeline.json';type='Timeline';guid='4e454d50524f44464c4f570000000302'}
        @{field='tone';name='Tone.wav';type='Audio';guid='4e454d50524f44464c4f570000000303'}
    )
    foreach ($item in $media) {
        Copy-Item -LiteralPath (Join-Path $source $item.name) -Destination $assets -Force
        Write-FlowJson (Join-Path $assets ($item.name+'.meta')) (New-FlowMeta $item.type $item.guid)
    }
    $fieldIDs=@{}
    $fields = @(foreach ($key in 'primary','alternate','token','select','overlay','motion','sequence','tone') {
        $fieldIDs[$key]='4e454d50-524f-4446-4c4f-57000000011'+$fieldIDs.Count
        @{ name=$key; fieldId=$fieldIDs[$key] }
    })
    $meta = New-FlowMeta 'Script' $scriptID
    $meta.scripts = @(@{fullTypeName='SceneFlow';scriptTypeId=$typeID;fields=$fields})
    Write-FlowJson (Join-Path $assets 'SceneFlow.cs.meta') $meta
    $runtime = Join-Path $repo 'Generated/Output/Release/Sandbox'
    $core = Join-Path $runtime 'Managed/NEM.ScriptCore.dll'
    $generator = Join-Path $repo 'Project/Engine/Managed/NEM.ScriptCodeGen/bin/Release/netstandard2.0/NEM.ScriptCodeGen.dll'
    [IO.File]::WriteAllText((Join-Path $game 'Scripts/GameScripts.csproj'), @"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><TargetFramework>net10.0</TargetFramework><Nullable>enable</Nullable><ImplicitUsings>enable</ImplicitUsings><OutputPath>../Managed/Release/</OutputPath><AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath></PropertyGroup>
  <ItemGroup><Reference Include="NEM.ScriptCore" HintPath="$core"/><Analyzer Include="$generator"/><Compile Include="../GameAssets/*.cs"/><AdditionalFiles Include="../GameAssets/*.cs.meta"/></ItemGroup>
</Project>
"@, $encoding)
    $tokenObject = New-FlowObject 'Token' '000000000000000a'
    $tokenObject.Components.TextRenderer = New-FlowText '*' 64
    Write-FlowJson (Join-Path $assets 'Token.prefab') @{
        SchemaVersion=2; Header=@{name='Token';rootLocalFileID=$tokenObject.LocalFileID;version=2}
        Entities=@($tokenObject);NestedPrefabInstances=@()
    }
    Write-FlowJson (Join-Path $assets 'Token.prefab.meta') (New-FlowMeta 'Prefab' $prefab)
    $routes=@{Title=@('Game','Select');Select=@('Game','Title');Game=@('Clear','Result');Clear=@('Title','Title');Result=@('NextStage','Select');NextStage=@('Game','Select')}
    $instructions=@{
        Title='Enter: start    Tab: select';Select='Enter: stage 1    Tab: title'
        Game="Space: collect a star    Enter: clear    Tab: result`nShift + Enter: stage select"
        Clear='Enter: title';Result='Enter: next stage    Tab: stage select';NextStage='Enter: play    Tab: stage select'
    }
    foreach ($name in $names) {
        $scene = Get-Content -LiteralPath (Join-Path $repo 'Templates/ManagedProject/2D/GameAssets/Scenes/Main.scene.json') -Raw | ConvertFrom-Json
        $scene.Header.name=$name
        $controller = New-FlowObject $name '0000000000000002'
        $values=@{}
        $values[$fieldIDs.primary]=@{value=@{assetId=$guid[$routes[$name][0]]}}
        $values[$fieldIDs.alternate]=@{value=@{assetId=$guid[$routes[$name][1]]}}
        $values[$fieldIDs.select]=@{value=@{assetId=$guid.Select}}
        $values[$fieldIDs.overlay]=@{value=@{assetId=$overlayGuid}}
        foreach ($item in $media) { $values[$fieldIDs[$item.field]]=@{value=@{assetId=$item.guid}} }
        $values[$fieldIDs.token]=@{value=@{assetId=$prefab}}
        $controller.Components.Script=@(@{enabled=$true; lastKnownTypeName='SceneFlow';scriptAsset=$scriptID;scriptTypeId=$typeID;scriptSlotId='0000000000000100';serializedFields=@{fields=$values}})
        $label=New-FlowObject 'Heading' '0000000000000003' 480 170
        $label.Components.TextRenderer=New-FlowText $name 54
        $hint=New-FlowObject 'Controls' '0000000000000004' 480 360
        $hint.Components.TextRenderer=New-FlowText $instructions[$name] 19
        $scene.Entities += $controller,$label,$hint
        Write-FlowJson (Join-Path $assets "Scenes/$name.scene.json") $scene
        Write-FlowJson (Join-Path $assets "Scenes/$name.scene.json.meta") (New-FlowMeta 'Scene' $guid[$name])
    }
    $overlay = @{SchemaVersion=3;Header=@{name='Overlay'; subScenes=@()};Entities=@((New-FlowObject 'OverlayMarker' '0000000000000044'));PrefabInstances=@()}
    Write-FlowJson (Join-Path $assets 'Scenes/Overlay.scene.json') $overlay
    Write-FlowJson (Join-Path $assets 'Scenes/Overlay.scene.json.meta') (New-FlowMeta 'Scene' $overlayGuid)
    [IO.File]::WriteAllText((Join-Path $assets 'RuntimeOnly/Items.csv'), "id,name`n1,Potion", $encoding)
    Write-FlowJson (Join-Path $assets 'RuntimeOnly/Items.csv.meta') (New-FlowMeta 'Text' '4e454d50524f44464c4f570000000401')
    Write-FlowJson (Join-Path $game 'ProjectSettings/Runtime/Game.json') @{additionalAssets=@('GameAssets/RuntimeOnly')}
    Write-FlowJson (Join-Path $game 'ProjectSettings/Runtime/StartupScene.json') @{activeScene=$guid.Title}
    if (!$Probe) { $Probe=Join-Path $repo 'Generated/Output/Debug/GameBuildServiceProbe/GameBuildServiceProbe.exe' }
    $env:NEMENGINE_ROOT=Join-Path $repo 'Project'
    $env:NEMENGINE_PROJECT=$descriptor
    & $Probe $game $guid.Title $manifestPath --manifest
    if ($LASTEXITCODE -ne 0) {throw 'シーン遷移の製品入力を収集できません'}
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $manifest.projectPath=$descriptor
    $manifest.prebuiltRuntime=$true
    $manifest.buildToolProject=''
    $manifest.gameWidth=960
    $manifest.gameHeight=540
    $manifest.productName='SceneFlow'
    $manifest.executableName='SceneFlow.exe'
    Write-FlowJson $manifestPath $manifest
    if ($PrepareOnly) {return}
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'Tools/BuildGame.ps1') -ManifestPath $manifestPath
    if ($LASTEXITCODE -ne 0) {throw 'シーン遷移ゲームをビルドできません'}
}
$product = Split-Path -Parent $exe
function Get-ProductSignature {
    return @(Get-ChildItem -LiteralPath $product -Recurse -File | Sort-Object FullName | ForEach-Object {
        $_.FullName.Substring($product.Length) + ' ' + (Get-FileHash -LiteralPath $_.FullName).Hash
    }) -join [Environment]::NewLine
}
$before=Get-ProductSignature
$documents = @(Get-ChildItem -LiteralPath $product -Recurse -File | Where-Object {
    $_.Extension -in '.txt','.md','.rtf' -or $_.BaseName -in 'COPYING','LICENSE','NOTICE','ATTRIBUTION'
})
if ($documents.Count) { throw "製品に単独の文書が含まれています: $($documents.FullName -join ', ')" }
. (Join-Path $PSScriptRoot 'HarnessProcess.ps1')
$notices = Invoke-HarnessProcess -FilePath $exe -Arguments @('--licenses') -WorkingDirectory $product -TimeoutSeconds 15
if ($notices.timedOut -or $notices.exitCode -ne 0 -or !$notices.output.Contains('FreeType') -or !$notices.output.Contains('.NET')) {
    throw 'ゲームの実行ファイルから配布表記を取得できません'
}
$userData=Join-Path $root ('User/' + [guid]::NewGuid().ToString('N').Substring(0,8))
foreach ($launch in 1,2) {
    $report = Join-Path $root "flow-report-$launch.txt"
    [IO.File]::WriteAllText($report,'',$encoding)
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$exe
    $start.WorkingDirectory=$repo
    $start.UseShellExecute=$false
    $start.RedirectStandardError=$true
    $start.RedirectStandardOutput=$true
    $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $start.EnvironmentVariables['NEM_PRODUCT_FLOW_REPORT']=$report
    $start.EnvironmentVariables['NEM_PRODUCT_RESTART']=if ($launch -eq 2) {'1'} else {'0'}
    $start.EnvironmentVariables['NEM_PRODUCT_COMPONENT_TEST']=if ($ComponentCheck -and $launch -eq 1) {'1'} else {'0'}
    $start.EnvironmentVariables['LOCALAPPDATA']=$userData
    $start.EnvironmentVariables['DOTNET_ROOT']=Join-Path $root 'NoGlobalDotNet'
    $start.EnvironmentVariables['NEMENGINE_TOOLCHAIN_ROOT']=Join-Path $root 'NoGlobalDotNet'
    $process=[Diagnostics.Process]::Start($start)
    $stdout=$process.StandardOutput.ReadToEndAsync()
    $stderr=$process.StandardError.ReadToEndAsync()
    try {
        if (!$process.WaitForExit(120000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "シーン遷移がタイムアウトしました: $report $($stderr.Result)"
        }
        if ($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $report -Pattern '^PASS$' -Quiet)) {
            throw "製品起動またはシーン遷移に失敗しました: $($process.ExitCode) $([IO.File]::ReadAllText($report))"
        }
    } finally {
        if ($process.HasExited) {
            [IO.File]::WriteAllText((Join-Path $root "game-$launch.log"), $stdout.Result + $stderr.Result, $encoding)
        }
        $process.Dispose()
    }
    if ($launch -eq 2 -and !(Select-String -LiteralPath $report -Pattern '^save restored$' -Quiet)) { throw 'セーブを復元できません' }
    Get-Content -LiteralPath $report
}
if ($before -cne (Get-ProductSignature)) {throw '実行中に製品フォルダーが書き換えられました'}
