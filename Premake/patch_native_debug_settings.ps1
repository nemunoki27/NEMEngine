param(
    [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)]
    [string[]]$ProjectPaths
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Ensure-ChildValue {
    param(
        $Parent,
        [string]$Name,
        [string]$Value
    )

    $child = $null
    foreach ($node in $Parent.ChildNodes) {
        if ($node.LocalName -eq $Name) {
            $child = $node
            break
        }
    }

    if ($null -eq $child) {
        $child = $Parent.OwnerDocument.CreateElement($Name, $Parent.NamespaceURI)
        [void]$Parent.AppendChild($child)
    }
    $child.InnerText = $Value
}

foreach ($projectPath in $ProjectPaths) {
    $resolved = Resolve-Path -LiteralPath $projectPath
    [xml]$document = Get-Content -LiteralPath $resolved.Path -Raw

    $condition = "'`$(Configuration)|`$(Platform)'=='Debug|x64'"
    $groups = $document.Project.ItemDefinitionGroup | Where-Object { $_.Condition -eq $condition }
    foreach ($group in $groups) {
        if ($null -ne $group.ClCompile) {
            # Mixed .NET Core debug時もC++の行情報を安定して読ませるため、Debugは/Ziに固定する。
            Ensure-ChildValue -Parent $group.ClCompile -Name "DebugInformationFormat" -Value "ProgramDatabase"
            Ensure-ChildValue -Parent $group.ClCompile -Name "SupportJustMyCode" -Value "false"
            Ensure-ChildValue -Parent $group.ClCompile -Name "Optimization" -Value "Disabled"
        }
        if ($null -ne $group.Link) {
            Ensure-ChildValue -Parent $group.Link -Name "GenerateDebugInformation" -Value "true"
        }
    }

    $xmlText = $document.OuterXml
    $xmlText = $xmlText.Replace(
        '<DebugInformationFormat>EditAndContinue</DebugInformationFormat>',
        '<DebugInformationFormat>ProgramDatabase</DebugInformationFormat>')

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($resolved.Path, $xmlText, $utf8NoBom)
    Write-Host "[OK] Patched native debug settings: $($resolved.Path)"
}
