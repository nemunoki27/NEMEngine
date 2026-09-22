function Sync-DirectoryContents([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
        throw "同期元フォルダーが見つかりません: $Source"
    }

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    robocopy $Source $Destination /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
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

