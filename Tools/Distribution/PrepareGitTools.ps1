param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$cache = Join-Path $repository 'Generated/Distribution/GitTools'
New-Item -ItemType Directory -Path $cache -Force | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Get-Package([string]$RepositoryName, [string]$Pattern, [string]$ID) {
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$RepositoryName/releases/latest" -Headers @{ 'User-Agent'='NEMLauncher-Package' }
    $assets = @($release.assets | Where-Object { $_.name -match $Pattern -and $_.name -notmatch 'busybox' })
    if ($assets.Count -ne 1) { throw "公式配布物を一つに特定できません: $RepositoryName" }
    $asset = $assets[0]
    if (!$asset.digest -or $asset.digest -notmatch '^sha256:[a-f0-9]{64}$') { throw "公式配布物のSHA-256がありません: $($asset.name)" }
    $expected = $asset.digest.Substring(7)
    $file = Join-Path $cache $asset.name
    if (!(Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $file -Headers @{ 'User-Agent'='NEMLauncher-Package' }
    }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw '公式配布物のSHA-256が一致しません' }
    $zip = [IO.Compression.ZipFile]::OpenRead($file)
    try {
        $total = ($zip.Entries | Measure-Object -Property Length -Sum).Sum
        $executables = @($zip.Entries | Where-Object { $_.FullName -match $(if ($ID -eq 'git') { '^cmd/git.exe$' } else { '(^|/)git-lfs.exe$' }) })
        if ($executables.Count -ne 1) { throw '配布物の実行ファイルを特定できません' }
        return [ordered]@{ version=$release.tag_name; executable=$executables[0].FullName;
            package=[ordered]@{id="$ID-$($release.tag_name.Replace('.','-'))";url=$asset.browser_download_url;
                sha256=$expected;bytes=[long]$asset.size;unpackedBytes=[long]$total} }
    } finally { $zip.Dispose() }
}

# 公式の安定版とハッシュを公開時に固定する
$git = Get-Package 'git-for-windows/git' '^MinGit-.+-64-bit\.zip$' 'git'
$lfs = Get-Package 'git-lfs/git-lfs' '^git-lfs-windows-amd64-v.+\.zip$' 'git-lfs'
$manifest = [ordered]@{id="git-$($git.package.sha256.Substring(0,12))-$($lfs.package.sha256.Substring(0,12))";
    gitVersion=$git.version;lfsVersion=$lfs.version;git=$git.package;lfs=$lfs.package;
    gitExecutable=$git.executable;lfsExecutable=$lfs.executable}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'GitTools.json'), ($manifest | ConvertTo-Json -Depth 10) + "`n")
Write-Host "Git $($git.version) / LFS $($lfs.version) の公式配布物を固定しました"
