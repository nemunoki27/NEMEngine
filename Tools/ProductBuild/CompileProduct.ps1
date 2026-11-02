function Invoke-ProductCompilation {
    param([string]$projectPath, [string]$sourceRuntime, [string]$buildToolProject, [string]$buildToolExecutable,
        [string]$gameScriptsProject, [bool]$prebuiltRuntime = $false, [string]$toolchainRoot = "")

    if (-not $prebuiltRuntime) {
        $msbuild = Get-MSBuildPath
        $nativeArguments = @(Get-NativeBuildArguments 'Release')
        Write-Output "ゲームのReleaseビルドを開始します"
        & $msbuild $projectPath @nativeArguments
        if ($LASTEXITCODE -ne 0) { throw "ゲームのReleaseビルドに失敗しました" }
    }

    # 製品には通常出力の最新DLLだけを使うためC#を強制再ビルドする
    Write-Output "C#ゲームスクリプトのReleaseビルドを確認します"
    $dotnet = if ($toolchainRoot) { Join-Path $toolchainRoot 'dotnet.exe' } else { 'dotnet' }
    & $dotnet build $gameScriptsProject -c Release --no-incremental -p:NEMScriptMetadataMode=EditorSync
    if ($LASTEXITCODE -ne 0) {
        throw "C#ゲームスクリプトのReleaseビルドに失敗しました"
    }

    $managedBuildOutput = Join-Path ([System.IO.Path]::GetDirectoryName($projectPath)) "Managed\Release"
    $gameScriptsDll = Join-Path $managedBuildOutput "GameScripts.dll"
    if (-not (Test-Path -LiteralPath $gameScriptsDll -PathType Leaf)) {
        throw "C#ゲームスクリプトのDLLが見つかりません: $gameScriptsDll"
    }

    if (-not [string]::IsNullOrWhiteSpace($buildToolProject)) {
        Write-Output "製品ビルドツールをビルドしています"
        & $msbuild $buildToolProject @nativeArguments
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $buildToolExecutable -PathType Leaf)) {
            throw "製品ビルドツールのビルドに失敗しました"
        }
    }
    else {
        Write-Output "同梱された製品ビルドツールを使用します"
    }

}
