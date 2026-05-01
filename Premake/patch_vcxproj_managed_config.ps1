param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectPath,

    [Parameter(Mandatory = $true)]
    [string]$ScriptCoreProjectPath,

    [Parameter(Mandatory = $true)]
    [string]$ScriptCoreManagedOutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $ProjectPath)) {
    throw "Visual C++ project was not found: $ProjectPath"
}

if (-not (Test-Path -LiteralPath $ScriptCoreProjectPath)) {
    throw "NEM.ScriptCore project was not found: $ScriptCoreProjectPath"
}

function Convert-ToCommandPath {
    param([string]$Path)
    return [System.IO.Path]::GetFullPath($Path).Replace("\", "/")
}

function Get-OrCreateChildElement {
    param(
        [xml]$Document,
        [System.Xml.XmlElement]$Parent,
        [string]$Name
    )

    foreach ($child in $Parent.ChildNodes) {
        if ($child.LocalName -eq $Name) {
            return $child
        }
    }

    $created = $Document.CreateElement($Name, $Parent.NamespaceURI)
    [void]$Parent.AppendChild($created)
    return $created
}

function Set-EventCommand {
    param(
        [xml]$Document,
        [System.Xml.XmlElement]$ItemDefinitionGroup,
        [string]$EventName,
        [string]$Command
    )

    $event = Get-OrCreateChildElement -Document $Document -Parent $ItemDefinitionGroup -Name $EventName
    $commandNode = Get-OrCreateChildElement -Document $Document -Parent $event -Name "Command"
    $commandNode.InnerText = $Command
}

$resolvedProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path
$scriptCoreProject = Convert-ToCommandPath $ScriptCoreProjectPath
$scriptCoreOutput = Convert-ToCommandPath $ScriptCoreManagedOutputPath

$preBuildCommand = @(
    'set DOTNET_CLI_UI_LANGUAGE=en',
    ('dotnet build "' + $scriptCoreProject + '" -c "$(Configuration)"'),
    'if exist "$(ProjectDir)Scripts\GameScripts.csproj" dotnet build "$(ProjectDir)Scripts\GameScripts.csproj" -c "$(Configuration)" --no-dependencies'
) -join "`r`n"

$postBuildCommand = @(
    'copy /Y "$(WindowsSdkDir)bin\$(TargetPlatformVersion)\x64\dxcompiler.dll" "$(TargetDir)dxcompiler.dll"',
    'copy /Y "$(WindowsSdkDir)bin\$(TargetPlatformVersion)\x64\dxil.dll" "$(TargetDir)dxil.dll"',
    ('if exist "' + $scriptCoreOutput + '\$(Configuration)\*" xcopy /Y /I "' + $scriptCoreOutput + '\$(Configuration)\*" "$(TargetDir)Managed\"'),
    'if exist "$(ProjectDir)Managed\$(Configuration)\*" xcopy /Y /I "$(ProjectDir)Managed\$(Configuration)\*" "$(TargetDir)Managed\"'
) -join "`r`n"

$document = New-Object xml
$document.PreserveWhitespace = $true
$document.Load($resolvedProjectPath)

$namespaceManager = New-Object System.Xml.XmlNamespaceManager($document.NameTable)
$namespaceManager.AddNamespace("msb", $document.DocumentElement.NamespaceURI)

$itemDefinitionGroups = $document.SelectNodes("//msb:ItemDefinitionGroup[msb:PreBuildEvent or msb:PostBuildEvent]", $namespaceManager)
if ($itemDefinitionGroups.Count -eq 0) {
    throw "No ItemDefinitionGroup with build events was found in: $resolvedProjectPath"
}

foreach ($group in $itemDefinitionGroups) {
    Set-EventCommand -Document $document -ItemDefinitionGroup $group -EventName "PreBuildEvent" -Command $preBuildCommand
    Set-EventCommand -Document $document -ItemDefinitionGroup $group -EventName "PostBuildEvent" -Command $postBuildCommand
}

$settings = [System.Xml.XmlWriterSettings]::new()
$settings.Indent = $true
$settings.Encoding = New-Object System.Text.UTF8Encoding($false)
$writer = [System.Xml.XmlWriter]::Create($resolvedProjectPath, $settings)
$document.Save($writer)
$writer.Close()

$patchedText = Get-Content -LiteralPath $resolvedProjectPath -Raw
if ($patchedText -notmatch '-c "\$\(Configuration\)"') {
    throw 'Managed build command verification failed: missing -c "$(Configuration)".'
}

if ($patchedText -notmatch 'Managed\\\$\(Configuration\)\\\*') {
    throw 'Managed copy command verification failed: missing Managed\$(Configuration)\*.'
}

if ($patchedText -match '-c "Debug"|Managed\\Debug\\\*|Release"\) else') {
    throw "Managed config verification failed: stale Debug/Release fallback command remains."
}

if ($patchedText -match 'dotnet build "\s+[^"]+\s+" -c|if exist "\s+[^"]+\s+\\\$\(Configuration\)') {
    throw "Managed config verification failed: command path contains unexpected line breaks."
}

Write-Host "[OK] Patched managed build/copy configuration: $resolvedProjectPath"
