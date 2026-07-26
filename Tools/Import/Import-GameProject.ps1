<#
.SYNOPSIS
    NEMEngineをサブモジュール参照しているゲームプロジェクトをエンジン内へ取り込み、
    Project/GameProjects/<name> へ複製してスタートアッププロジェクトとして起動できる状態にする。

.DESCRIPTION
    ゲーム本体(<repo>/Project/<name>)だけを複製し、エンジンソースを直リンクする構成にする。
    これによりゲーム実行中にエンジン側へブレークポイントを置いてデバッグできる(SDKのDLLではなくソースをビルドするため)。

    - SDK(External/NEMEngine)サブモジュールは取得しない(--recurse-submodulesしない)。エンジンはこのリポジトリ自身を使う。
    - Scripts/GameScripts.csproj のエンジン参照を SDK(Reference/HintPath) から エンジンソース(ProjectReference) へ書き換える。
    - 取り込んだフォルダに Update<name>Project.bat を生成する。これで後から最新Gitへ更新できる。
    - 最後に Premake/generate_vs2026.bat を実行してソリューションへ反映する。

.PARAMETER GitUrl
    取り込むゲームプロジェクトのGitリポジトリURL。

.PARAMETER Update
    既存の取り込みプロジェクトを最新Gitへ更新する。ソース/アセット/共有設定のみ上書きし、UserSettings/Library/Saved/Managedはローカル保持する。

.PARAMETER ProjectName
    プロジェクト名を明示する(主にUpdate用、または1リポジトリに複数ゲームがある場合の選択)。

.PARAMETER SkipGenerate
    取り込み/更新後の generate_vs2026.bat 実行を省略する。
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$GitUrl,

    [switch]$Update,

    [string]$ProjectName = "",

    [switch]$SkipGenerate
)

$ErrorActionPreference = "Stop"

# Tools/Import/ から見てリポジトリルートは2つ上
$engineRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
$gameProjectsRoot = Join-Path $engineRoot "Project\GameProjects"
$generateBat = Join-Path $engineRoot "Premake\generate_vs2026.bat"

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git が見つかりません。Gitをインストールし、PATHを通してから再実行してください。"
}

# robocopyはディレクトリ単位のミラーで、終了コード8以上が実エラー(0-7は正常)。
function Invoke-Robocopy {
    param(
        [string]$Source,
        [string]$Destination,
        [string[]]$ExcludeDirs,
        [string[]]$ExcludeFiles
    )

    $roboArgs = @($Source, $Destination, "/E", "/NFL", "/NDL", "/NJH", "/NJS", "/NP", "/R:1", "/W:1")
    if ($ExcludeDirs.Count -gt 0) {
        $roboArgs += "/XD"
        $roboArgs += $ExcludeDirs
    }
    if ($ExcludeFiles.Count -gt 0) {
        $roboArgs += "/XF"
        $roboArgs += $ExcludeFiles
    }

    & robocopy @roboArgs | Out-Null
    if ($LASTEXITCODE -ge 8) {
        throw "ファイル複製に失敗しました(robocopy exit code=$LASTEXITCODE): $Source -> $Destination"
    }
}

# GameScripts.csproj のエンジン参照を SDK 版から エンジンソース版(ProjectReference)へ書き換える。
function Convert-GameScriptsCsproj {
    param([string]$CsprojPath)

    if (-not (Test-Path -LiteralPath $CsprojPath)) {
        Write-Host "[SKIP] GameScripts.csproj が見つかりません: $CsprojPath"
        return
    }

    [xml]$csproj = Get-Content -LiteralPath $CsprojPath -Raw
    $root = $csproj.Project

    # SDKパスを保持していたPropertyGroup(NEMEngineSdkManaged)を削除する
    foreach ($pg in @($root.ChildNodes | Where-Object { $_.LocalName -eq 'PropertyGroup' })) {
        $hasSdkManaged = @($pg.ChildNodes | Where-Object { $_.LocalName -eq 'NEMEngineSdkManaged' }).Count -gt 0
        if ($hasSdkManaged) {
            [void]$root.RemoveChild($pg)
        }
    }

    # SDKのDLLを指す Reference / Analyzer をすべて除去する(スクリプトプロジェクトはエンジン以外を参照しない前提)
    foreach ($ig in @($root.ChildNodes | Where-Object { $_.LocalName -eq 'ItemGroup' })) {
        foreach ($child in @($ig.ChildNodes)) {
            if ($child.LocalName -eq 'Reference' -or $child.LocalName -eq 'Analyzer') {
                [void]$ig.RemoveChild($child)
            }
        }
    }

    # Compile を持つ主ItemGroupへ、エンジンソースのProjectReferenceを追加する
    $targetIg = @($root.ChildNodes | Where-Object {
        $_.LocalName -eq 'ItemGroup' -and (@($_.ChildNodes | Where-Object { $_.LocalName -eq 'Compile' }).Count -gt 0)
    }) | Select-Object -First 1
    if ($null -eq $targetIg) {
        $targetIg = $csproj.CreateElement('ItemGroup')
        [void]$root.AppendChild($targetIg)
    }

    function New-ProjectReference {
        param([string]$Include, [switch]$AsAnalyzer)

        $pr = $csproj.CreateElement('ProjectReference')
        $pr.SetAttribute('Include', $Include)
        if ($AsAnalyzer) {
            $outputType = $csproj.CreateElement('OutputItemType')
            $outputType.InnerText = 'Analyzer'
            [void]$pr.AppendChild($outputType)
            $refOutput = $csproj.CreateElement('ReferenceOutputAssembly')
            $refOutput.InnerText = 'false'
            [void]$pr.AppendChild($refOutput)
        }
        return $pr
    }

    # 取り込み先は Project/GameProjects/<name>/<name>/Scripts なので、エンジンソースまでは ..\..\..\..\Engine。
    [void]$targetIg.AppendChild((New-ProjectReference '..\..\..\..\Engine\Managed\NEM.ScriptCore\NEM.ScriptCore.csproj'))
    [void]$targetIg.AppendChild((New-ProjectReference '..\..\..\..\Engine\Managed\NEM.ScriptCodeGen\NEM.ScriptCodeGen.csproj' -AsAnalyzer))
    [void]$targetIg.AppendChild((New-ProjectReference '..\..\..\..\Engine\Managed\NEM.ScriptAnalyzers\NEM.ScriptAnalyzers.csproj' -AsAnalyzer))

    $settings = [System.Xml.XmlWriterSettings]::new()
    $settings.Indent = $true
    $settings.OmitXmlDeclaration = $true
    $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
    $writer = [System.Xml.XmlWriter]::Create($CsprojPath, $settings)
    $csproj.Save($writer)
    $writer.Close()

    Write-Host "[OK] GameScripts.csproj をエンジンソース参照へ書き換えました: $CsprojPath"
}

# 取り込んだフォルダへ Update<name>Project.bat を書き出す
function Write-UpdateBatch {
    param(
        [string]$DestinationDir,
        [string]$Name,
        [string]$Url
    )

    $batPath = Join-Path $DestinationDir ("Update" + $Name + "Project.bat")
    # 注意: @(...) 配列リテラル内で '文字列' + $変数 + '文字列' と連結すると、
    # + $変数 が単項プラス扱いされ別々の配列要素に分割される(1行のはずが複数要素になりバッチが壊れる)。
    # 変数を含む行は必ず文字列補間("... $Name ...")で1要素の文字列として組み立てること。
    $lines = @(
        '@echo off',
        'setlocal',
        "rem $Name を最新のGit状態へ更新する。",
        'rem ソース/アセット/共有設定のみ上書きし、UserSettings/Library/Saved/Managed はローカル保持する。',
        'rem このファイルは Tools/Import/Import-GameProject.ps1 が自動生成している。',
        "powershell -NoProfile -ExecutionPolicy Bypass -File `"%~dp0..\..\..\Tools\Import\Import-GameProject.ps1`" -GitUrl `"$Url`" -ProjectName `"$Name`" -Update",
        'set "RC=%ERRORLEVEL%"',
        'if not "%RC%"=="0" (',
        "    echo [ERROR] $Name の更新に失敗しました。",
        '    pause',
        '    exit /b %RC%',
        ')',
        "echo [OK] $Name を更新しました。",
        'endlocal'
    )
    # cmd(日本語コンソール=cp932)で文字化けせず実行できるよう Shift-JIS(cp932)/CRLF で書き出す
    $content = ($lines -join "`r`n") + "`r`n"
    [System.IO.File]::WriteAllText($batPath, $content, [System.Text.Encoding]::GetEncoding(932))
    Write-Host "[OK] 更新バッチを生成しました: $batPath"
}

# 一時フォルダへ浅くクローン(SDKサブモジュールは取得しない)
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("nem_import_" + [System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null

try {
    Write-Host "===== クローン中: $GitUrl ====="
    & git clone --depth 1 $GitUrl $tempRoot
    if ($LASTEXITCODE -ne 0) {
        throw "git clone に失敗しました: $GitUrl"
    }

    # GameAssetsを持つフォルダ(=ゲーム本体)を探す。SDK配下(External)は除外する。
    $assetDirs = Get-ChildItem -LiteralPath $tempRoot -Recurse -Directory -Filter 'GameAssets' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '[\\/]External[\\/]' }

    $appRoots = @($assetDirs | ForEach-Object { $_.Parent })
    if ($appRoots.Count -eq 0) {
        throw "クローンしたリポジトリ内に GameAssets を持つゲームプロジェクトが見つかりませんでした。"
    }

    # ProjectName指定があれば一致するものに絞る
    if (-not [string]::IsNullOrWhiteSpace($ProjectName)) {
        $appRoots = @($appRoots | Where-Object { $_.Name -ieq $ProjectName })
        if ($appRoots.Count -eq 0) {
            throw "指定された ProjectName '$ProjectName' に一致するゲームプロジェクトが見つかりませんでした。"
        }
    }

    if ($appRoots.Count -gt 1) {
        $names = ($appRoots | ForEach-Object { $_.Name }) -join ", "
        throw "複数のゲームプロジェクトが見つかりました($names)。-ProjectName で対象を指定してください。"
    }

    $appRoot = $appRoots[0]
    $name = $appRoot.Name

    # コンテナ(<name>)の中にアプリ本体(<name>)を置く1段ネスト構成。
    # コンテナを実行時の作業ディレクトリにして、直下のappにある.nemprojectを一意に解決する。
    $container = Join-Path $gameProjectsRoot $name
    $destination = Join-Path $container $name

    if ($Update) {
        if (-not (Test-Path -LiteralPath $destination)) {
            throw "更新対象 '$name' が $container に存在しません。先に取り込みを実行してください。"
        }
    }

    New-Item -ItemType Directory -Force -Path $destination | Out-Null

    # 常に除外する作業/生成物。Managedはビルド出力なので取り込まない(ビルド時に再生成される)。
    $excludeDirs = @(
        ".vs",
        "obj",
        "bin",
        "Generated",
        (Join-Path $appRoot.FullName "Managed"),
        (Join-Path $appRoot.FullName "Library"),
        (Join-Path $appRoot.FullName "Saved"),
        (Join-Path $appRoot.FullName "UserSettings")
    )
    $excludeFiles = @("*.vcxproj", "*.vcxproj.filters", "*.vcxproj.user")

    Write-Host "===== 複製中: $name -> $destination ====="
    Invoke-Robocopy -Source $appRoot.FullName -Destination $destination -ExcludeDirs $excludeDirs -ExcludeFiles $excludeFiles

    # エンジンソース参照へ書き換える
    Convert-GameScriptsCsproj -CsprojPath (Join-Path $destination "Scripts\GameScripts.csproj")

    # 更新バッチはコンテナ直下に置く(ユーザーが GameProjects/<name>/Update<name>Project.bat を叩く)。
    Write-UpdateBatch -DestinationDir $container -Name $name -Url $GitUrl

    Write-Host "[OK] $name を $destination へ取り込みました。"
}
finally {
    # 一時クローンを削除する
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if ($SkipGenerate) {
    Write-Host "[SKIP] generate_vs2026.bat の実行を省略しました。手動で再生成してください。"
    return
}

Write-Host "===== ソリューション再生成中 ====="
& $generateBat
if ($LASTEXITCODE -ne 0) {
    throw "generate_vs2026.bat に失敗しました。出力を確認してください。"
}

Write-Host "[DONE] 取り込み/更新が完了しました。Visual Studioでソリューションを開き直し、スタートアッププロジェクトを切り替えて実行してください。"
