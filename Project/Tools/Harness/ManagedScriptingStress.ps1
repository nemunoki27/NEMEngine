<#
.SYNOPSIS
  Managed scripting の非 GUI stress / regression harness。
  GUI(Editor) を起動せず実行可能な範囲を deterministic に回す。

.DESCRIPTION
  以下を実行する:
   1. managed build / metadata sync 反復（-Iterations 回）。各回の所要時間を集計。
   2. intentional build failure -> recovery（syntax error 注入 -> build 失敗確認 -> 復旧 -> build 成功）。
   3. component generator --verify と --selftest（負例）。
   4. analyzer test harness（good/bad fixture）。
  GUI を要する runtime store（exception/profiler/inspector cache/execution order 実行時）の stress は
  本 harness の対象外（要 runtime。README/docs 参照）。

.PARAMETER Mode
  quick(10) / standard(100) / extended(1000)。-Iterations 指定時はそちらを優先。

.EXAMPLE
  pwsh -File ManagedScriptingStress.ps1 -Mode quick
#>
param(
    [ValidateSet("quick", "standard", "extended")]
    [string]$Mode = "quick",
    [int]$Iterations = 0
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$gameScripts = Join-Path $root "Project\Sandbox\Scripts\GameScripts.csproj"
$gen = Join-Path $root "Project\Tools\NEM.ComponentBindingGen\NEM.ComponentBindingGen.csproj"
$analyzerTests = Join-Path $root "Project\Engine\Managed\NEM.ScriptAnalyzers.Tests\NEM.ScriptAnalyzers.Tests.csproj"
$bindings = Join-Path $root "Project\Engine\Core\Scripting\Managed\Bindings"

if ($Iterations -le 0) {
    $Iterations = switch ($Mode) { "quick" { 10 } "standard" { 100 } "extended" { 1000 } }
}

$failures = 0
function Check($name, $ok) {
    if ($ok) { Write-Host "[PASS] $name" } else { $script:failures++; Write-Host "[FAIL] $name" -ForegroundColor Red }
}

Write-Host "=== ManagedScriptingStress: Mode=$Mode Iterations=$Iterations ==="

# --- 1. managed build / reload 反復 ---
$times = @()
for ($i = 1; $i -le $Iterations; $i++) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    dotnet build $gameScripts -c Debug -nologo -v q | Out-Null
    $rc = $LASTEXITCODE
    $sw.Stop()
    $times += $sw.Elapsed.TotalMilliseconds
    if ($rc -ne 0) { $failures++; Write-Host "[FAIL] build iteration $i (exit $rc)" -ForegroundColor Red; break }
}
if ($times.Count -gt 0) {
    $stats = $times | Measure-Object -Average -Minimum -Maximum
    Write-Host ("[info] build x{0}: mean={1:N1}ms min={2:N1}ms max={3:N1}ms" -f $times.Count, $stats.Average, $stats.Minimum, $stats.Maximum)
    Check "managed build repeated ($($times.Count) iterations)" ($times.Count -eq $Iterations)
}

# --- 2. intentional build failure -> recovery ---
$probe = Join-Path $root "Project\Sandbox\GameAssets\Scripts\__StressBreak.cs"
try {
    Set-Content -LiteralPath $probe -Value "this is not valid C#" -NoNewline
    dotnet build $gameScripts -c Debug -nologo -v q | Out-Null
    $failedAsExpected = ($LASTEXITCODE -ne 0)
    Check "intentional build failure detected" $failedAsExpected
} finally {
    Remove-Item -LiteralPath $probe -Force -ErrorAction SilentlyContinue
}
dotnet build $gameScripts -c Debug -nologo -v q | Out-Null
Check "build recovers after fixing source" ($LASTEXITCODE -eq 0)

# --- 3. generator verify + selftest ---
dotnet run --project $gen -c Release -- --verify `
    --manifest (Join-Path $bindings "ComponentManifest.json") `
    --abi (Join-Path $bindings "ManagedNativeApi.json") `
    --out-native-dir (Join-Path $root "Project\Engine\Core\Scripting\Managed\Generated") `
    --out-cs-dir     (Join-Path $root "Project\Engine\Managed\NEM.ScriptCore\Generated") | Out-Null
Check "generator --verify" ($LASTEXITCODE -eq 0)

dotnet run --project $gen -c Release -- --selftest | Out-Null
Check "generator --selftest (negative cases)" ($LASTEXITCODE -eq 0)

# --- 4. analyzer test harness ---
dotnet run --project $analyzerTests -c Debug | Out-Null
Check "analyzer good/bad fixture tests" ($LASTEXITCODE -eq 0)

Write-Host "=== ManagedScriptingStress done. failures=$failures ==="
exit $failures
