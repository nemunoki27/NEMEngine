. (Join-Path $PSScriptRoot '..\FileSystem\DirectorySafety.ps1')

function Sync-DirectoryContents([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
        throw "同期元フォルダーが見つかりません: $Source"
    }

    Assert-SeparateDirectories $Source $Destination
    Assert-DirectoryTreeWithoutLinks $Source
    Assert-DirectoryTreeWithoutLinks $Destination
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    robocopy $Source $Destination /MIR /XJ /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
    if ($LASTEXITCODE -ge 8) {
        throw "フォルダーの同期に失敗しました: $Source -> $Destination code=$LASTEXITCODE"
    }
}

function Get-FileSHA256([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        try {
            return [System.BitConverter]::ToString($sha256.ComputeHash($stream)).Replace("-", "")
        }
        finally {
            $sha256.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Assert-ManagedDeployment([string]$Source, [string]$Destination, [string]$Configuration) {
    $sourceDll = Join-Path $Source "NEM.ScriptCore.dll"
    $destinationDll = Join-Path $Destination "NEM.ScriptCore.dll"
    if (-not (Test-Path -LiteralPath $destinationDll -PathType Leaf)) {
        throw "NEM.ScriptCore.dllを配置できませんでした: $destinationDll"
    }
    if ((Get-FileSHA256 $sourceDll) -ne (Get-FileSHA256 $destinationDll)) {
        throw "NEM.ScriptCore.dllの配置結果がビルド成果物と一致しません: $destinationDll"
    }

    $nestedConfiguration = Join-Path $Destination $Configuration
    if (Test-Path -LiteralPath $nestedConfiguration) {
        throw "Managedフォルダーが二重階層になっています: $nestedConfiguration"
    }
}

function Publish-SDKDirectory([string]$Stage, [string]$Destination) {

    # 作成済みのSDKだけを差し替える
    $stageFull = [System.IO.Path]::GetFullPath($Stage).TrimEnd('\', '/')
    $destinationFull = [System.IO.Path]::GetFullPath($Destination).TrimEnd('\', '/')
    if ([System.IO.Path]::GetDirectoryName($stageFull) -ne [System.IO.Path]::GetDirectoryName($destinationFull) -or
        -not [System.IO.Path]::GetFileName($stageFull).StartsWith('.nem-sdk-stage-')) {
        throw "SDKの差し替え元が不正です: $Stage"
    }
    Assert-SeparateDirectories $stageFull $destinationFull
    Assert-DirectoryTreeWithoutLinks $stageFull
    Assert-DirectoryTreeWithoutLinks $destinationFull
    if (-not (Test-Path -LiteralPath (Join-Path $stageFull 'sdk_version.json') -PathType Leaf)) {
        throw '完成したSDKの版情報がありません'
    }
    if (Test-Path -LiteralPath $destinationFull) {
        $existing = Get-Item -LiteralPath $destinationFull
        if (-not $existing.PSIsContainer -or ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "SDKの差し替え先にリンクやファイルは使用できません: $Destination"
        }
        if (@(Get-ChildItem -LiteralPath $destinationFull -Force).Count -and
            -not (Test-Path -LiteralPath (Join-Path $destinationFull 'sdk_version.json') -PathType Leaf)) {
            throw "既存のフォルダーはNEMEngine SDKではありません: $Destination"
        }
        foreach ($binaryRoot in @('Bin', 'Editor', 'Runtime', 'Tools')) {
            $path = Join-Path $destinationFull $binaryRoot
            if (-not (Test-Path -LiteralPath $path)) { continue }
            foreach ($file in Get-ChildItem -LiteralPath $path -Recurse -File) {
                if ($file.Extension -notin @('.exe', '.dll')) { continue }
                $stream = [IO.File]::Open($file.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
                $stream.Dispose()
            }
        }
        # 公開先のGit管理情報は内容を変えず引き継ぐ
        $gitMetadata = Join-Path $destinationFull '.git'
        if (Test-Path -LiteralPath $gitMetadata) {
            Copy-Item -LiteralPath $gitMetadata -Destination $stageFull -Recurse -Force
        }
    }
    $backup = Join-Path ([IO.Path]::GetDirectoryName($destinationFull)) ('.nem-sdk-backup-' + [Guid]::NewGuid().ToString('N'))
    if (Test-Path -LiteralPath $destinationFull) {
        Move-Item -LiteralPath $destinationFull -Destination $backup
    }
    try {
        Move-Item -LiteralPath $stageFull -Destination $destinationFull
    } catch {
        # 公開に失敗した場合は旧SDKへ戻す
        if (Test-Path -LiteralPath $backup) { Move-Item -LiteralPath $backup -Destination $destinationFull }
        throw
    }
    if (Test-Path -LiteralPath $backup) {
        # 公開済みのSDKは退避先の削除失敗で失敗扱いにしない
        try { Remove-Item -LiteralPath $backup -Recurse -Force }
        catch { Write-Warning "SDKの公開は完了しましたが退避先を削除できません: $backup" }
    }
}

