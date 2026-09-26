# 未実行と失敗を基準検証で区別する
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 外部ビルドを起動せず判定処理だけ読み込む
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'RefactoringBaseline.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) { throw $parseErrors[0] }
foreach ($name in @('Get-ComparableMetrics', 'New-Baseline', 'Test-Baseline')) {
    $function = $ast.Find({ param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $false)
    if ($null -eq $function) { throw "判定関数がありません: $name" }
    . ([scriptblock]::Create($function.Extent.Text))
}

function Assert-Result([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
    Write-Host "[PASS] $message"
}

$report = [pscustomobject]@{
    scope = 'Static'
    source = @{}
    assets = @{ parseErrors = 0 }
    shaders = @{ referenceErrors = 0 }
    abi = @{ failures = 0 }
    risks = @{
        cppIncludeCaseMismatches = @(); shaderIncludeCaseMismatches = @()
        coreEditorIncludeFiles = 0; coreGuiFiles = 0
    }
    shaderCompile = $null; managed = $null; build = $null
}
$baseline = New-Baseline $report
$baseline = [pscustomobject]@{ limits = [pscustomobject]$baseline.limits }
$metrics = Get-ComparableMetrics $report
Assert-Result ($null -eq $metrics.buildFailures) '未実行のビルドは成功数値にならない'
Assert-Result (@(Test-Baseline $report $baseline).Count -eq 0) '静的検証は未実行の動的検証を比較しない'
$report.scope = 'Full'
Assert-Result (@(Test-Baseline $report $baseline).Count -eq 3) '全検証の未実行は三件とも失敗になる'
$report.shaderCompile = @{ failures = 0 }
$report.managed = @{ failures = 0 }
$report.build = @{ failures = 1 }
Assert-Result (@(Test-Baseline $report $baseline).Count -eq 1) 'ビルド失敗を基準検証で検出する'
$report.build.failures = 0
Assert-Result (@(Test-Baseline $report $baseline).Count -eq 0) '実行済みの全検証成功を受け入れる'
$report.abi.failures = 1
$captured = New-Baseline $report
$captured = [pscustomobject]@{ limits = [pscustomobject]$captured.limits }
Assert-Result (@(Test-Baseline $report $captured).Count -eq 1) '基準保存でもABI失敗を受け入れない'
exit 0
