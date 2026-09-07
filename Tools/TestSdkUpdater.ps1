# SDK更新のGit制御を模擬し、実際のリポジトリは変更しない
$ErrorActionPreference = "Stop"
$source = Join-Path $PSScriptRoot "..\Templates\GameProject\Tools\UpdateSdk.ps1"
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    $source, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
foreach ($name in @("Invoke-SdkGit", "Update-SdkRepository", "Assert-SdkNotInUse")) {
    $node = $ast.Find({ param($item)
        $item -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $item.Name -eq $name
    }, $true)
    . ([scriptblock]::Create($node.Extent.Text))
}

$externalEngine = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\Generated\SDK"))
function Test-Path { return $true }
function git {
    $command = ($args | Select-Object -Skip 2) -join ' '
    $script:calls.Add($command)
    $global:LASTEXITCODE = 0
    if ($command -like "$script:failure*") {
        $global:LASTEXITCODE = 1
        return "simulated failure"
    }
    switch ($command) {
        "rev-parse --show-toplevel" { return $externalEngine }
        "rev-parse --verify origin/main^{commit}" { return "new" }
        "rev-parse HEAD" { return $script:head }
        "status --porcelain --untracked-files=all --ignored" { if ($script:dirty) { return " M Editor/Debug/NEMEditor.exe" } }
        "rev-parse refs/stash" { return "backup" }
        "reset --hard new" { $script:head = "new" }
    }
}
foreach ($failure in @("none", "fetch", "stash", "reset", "submodule", "diff")) {
    $script:failure = $failure
    $script:calls = [System.Collections.Generic.List[string]]::new()
    $script:head = "old"
    $script:dirty = $true
    $failed = $false
    try { Update-SdkRepository } catch { $failed = $true }
    if ($failed -ne ($failure -ne "none")) { throw "Failure detection: $failure" }
    if ($failure -ne "none" -and $script:calls[-1] -notlike "$failure*") {
        throw "Continued after failure: $failure"
    }
    if ($failure -eq "none" -and $script:head -ne "new") { throw "HEAD mismatch" }
    Write-Host "PASS Git $failure"
}

function Get-GameProjectName { return "GJ4" }
$script:running = $true
function Get-Process { if ($script:running) { return [pscustomobject]@{ Id = 123 } } }
$blocked = $false
try { Assert-SdkNotInUse } catch { $blocked = $true }
if (-not $blocked) { throw "Running Editor was not blocked" }
Write-Host "PASS running process guard"

# 実行ファイルと同じ排他オープンで、ロック時と解除後を確認
$script:running = $false
function Get-ChildItem { return [pscustomobject]@{ Extension = ".exe"; FullName = $source } }
$lockedFile = [System.IO.File]::OpenRead($source)
try {
    $blocked = $false
    try { Assert-SdkNotInUse } catch { $blocked = $true }
    if (-not $blocked) { throw "Locked file was not blocked" }
} finally {
    $lockedFile.Dispose()
}
Write-Host "PASS file lock guard"
Assert-SdkNotInUse
Write-Host "PASS unlocked file"
