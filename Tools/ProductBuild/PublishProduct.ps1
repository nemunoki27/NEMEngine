. (Join-Path $PSScriptRoot 'ProductNotices.ps1')

function Copy-ProductDotNet {
    param([string]$toolchainRoot, [string]$managed, [string]$destination)

    $config = Get-Content -LiteralPath (Join-Path $managed 'NEM.ScriptCore.runtimeconfig.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $framework = $config.runtimeOptions.framework
    $required = [version]$framework.version
    foreach ($relative in @("shared/$($framework.name)", 'host/fxr')) {
        $source = Get-ChildPath $toolchainRoot $relative
        $match = Get-ChildItem -LiteralPath $source -Directory | Where-Object {
            $version = $null
            [version]::TryParse($_.Name, [ref]$version) -and $version.Major -eq $required.Major -and
                $version.Minor -eq $required.Minor -and $required -le $version
        } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
        if (-not $match) { throw ".NET $required のランタイムがありません: $source" }
        $folder = Get-ChildPath $destination $relative
        New-Item -ItemType Directory -Path $folder -Force | Out-Null
        $versionRoot = Get-ChildPath $folder $match.Name
        New-Item -ItemType Directory -Path $versionRoot -Force | Out-Null
        foreach ($item in Get-ChildItem -LiteralPath $match.FullName -File -Recurse) {
            if ($item.Name -notmatch '(\.dll|\.deps\.json|\.runtimeconfig\.json)$' -or $item.Name -match '^(mscordbi|mscordaccore(_.*)?|dbgshim)\.dll$') { continue }
            $target = Get-ChildPath $versionRoot ($item.FullName.Substring($match.FullName.Length + 1))
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
            Copy-Item -LiteralPath $item.FullName -Destination $target
        }
    }
}

function New-PackedProduct {
    param($manifest, [string]$stage, [string]$destination, [string]$exeName, [string]$tool, [string]$manifestPath, [string]$toolchainRoot, [bool]$managedScripts = $true)
    $runtime = Get-ChildPath $destination 'Data/Runtime'
    New-Item -ItemType Directory -Path $runtime -Force | Out-Null
    $pack = Get-ChildPath $destination 'Data/Content.nempak'
    & $tool --pack-product $stage $pack $manifestPath
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $pack -PathType Leaf)) { throw '製品アセットのパックに失敗しました' }
    & $tool --verify-product $pack ([string]$manifest.projectGuid)
    if ($LASTEXITCODE -ne 0) { throw '製品アセットの検証に失敗しました' }

    Copy-Item -LiteralPath (Get-ChildPath $stage $exeName) -Destination $destination
    $dependencies = Get-Content -LiteralPath (Join-Path $stage 'nem.runtime-dependencies.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($file in $dependencies.files) {
        $path = Get-ChildPath $runtime $file
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        Copy-Item -LiteralPath (Get-ChildPath $stage $file) -Destination $path
    }
    if ($managedScripts) {
        $managed = Get-ChildPath $stage 'Managed'
        Copy-Item -LiteralPath $managed -Destination $runtime -Recurse
        if (-not $toolchainRoot) {
            $toolchainRoot = Split-Path -Parent (Get-Command dotnet.exe -CommandType Application | Select-Object -First 1).Source
        }
        Copy-ProductDotNet $toolchainRoot $managed (Join-Path $runtime 'DotNet')
    }
    Set-ProductNotices $manifest $stage (Get-ChildPath $destination $exeName) $toolchainRoot $managedScripts
}

function Publish-PackedProduct {
    param([string]$outputRoot, [string]$targetName, [string]$publishName, [string]$backupName,
        [string]$tool, [string]$projectGuid, [string]$executableName)
    $target = Get-ChildPath $outputRoot $targetName
    $publish = Get-ChildPath $outputRoot $publishName
    $backup = Get-ChildPath $outputRoot $backupName
    $preserveBackup = $false
    if (Test-Path -LiteralPath $target) {
        if ((Get-Item -LiteralPath $target).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "リンク先には配置できません: $target" }
        $pack = Get-ChildPath $target 'Data/Content.nempak'
        if (Test-Path -LiteralPath $pack -PathType Leaf) {
            & $tool --verify-product $pack $projectGuid
            if ($LASTEXITCODE -ne 0) { throw "既存の製品パックを検証できません: $target" }
        } else {
            $descriptor = Get-ChildPath $target ([IO.Path]::ChangeExtension($executableName, '.nemproject'))
            if (-not (Test-Path -LiteralPath (Join-Path $target '.nemBuildManifest.json') -PathType Leaf) -or
                -not (Test-Path -LiteralPath $descriptor -PathType Leaf) -or
                (Get-Content -LiteralPath $descriptor -Raw -Encoding UTF8 | ConvertFrom-Json).projectGuid -ne $projectGuid) {
                throw "同じプロジェクトの製品ビルド以外は上書きできません: $target"
            }
        }
        foreach ($directory in 'Saved','UserSettings') {
            $data = Get-ChildPath $target $directory
            if ((Test-Path -LiteralPath $data) -and @(Get-ChildItem -LiteralPath $data -Force | Where-Object { $_.Name -ne 'Logs' }).Count) {
                $preserveBackup = $true
            }
        }
        [IO.Directory]::Move($target, $backup)
    }
    try { [IO.Directory]::Move($publish, $target) }
    catch {
        if ((Test-Path -LiteralPath $backup) -and -not (Test-Path -LiteralPath $target)) { [IO.Directory]::Move($backup, $target) }
        throw
    }
    if (Test-Path -LiteralPath $backup) {
        if ($preserveBackup) { Write-Output "旧製品の保存データを保管しました: $backup" }
        else {
            try { Remove-Item -LiteralPath $backup -Recurse -Force }
            catch { Write-Warning "更新前のフォルダーを削除できません: $backup ($($_.Exception.Message))" }
        }
    }
}
