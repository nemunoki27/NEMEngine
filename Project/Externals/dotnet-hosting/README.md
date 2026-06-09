# dotnet-hosting (vendored .NET native hosting files)

`nethost` / `hostfxr` の公式 native hosting ヘッダと `nethost.dll` を vendor したもの。
`DotnetHostResolver`（`Engine/Core/Scripting/Managed/DotnetHostResolver.*`）が
`get_hostfxr_path()` で hostfxr を解決するために使用する。

## vendor したファイル

```
include/nethost.h
include/hostfxr.h
include/coreclr_delegates.h
bin/x64/nethost.dll
LICENSE.txt        (.NET ランタイムの MIT ライセンス + attribution)
```

ヘッダと `nethost.dll` は同一の package version（下記）に由来する。

## 取得元

- 公式 package 名: `Microsoft.NETCore.App.Host.win-x64`
- package version: `10.0.8`
- RID: `win-x64`
- 取得元: インストール済み .NET SDK の host pack
  `%ProgramFiles%\dotnet\packs\Microsoft.NETCore.App.Host.win-x64\10.0.8\runtimes\win-x64\native\`
- ライセンス: MIT（各ヘッダ先頭表記、および `LICENSE.txt`）

### `nethost.dll` の SHA-256

```
E36F2F5E5B6074734CA8C58C2EFBFB19D043371F5986920E335FFCCA849019E5
```

## リンク / ロード方法

`nethost` は**リンクしない**。`DotnetHostResolver` が実行時に exe ディレクトリ直下の
`nethost.dll` を**絶対パス**で `LoadLibraryExW`
（`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`）でロードし、
`GetProcAddress("get_hostfxr_path")` で関数を取得する。

> 静的 `libnethost.lib` はリリース静的 CRT（`/MT`）固定で配布されており、本プロジェクトの
> Debug（`/MTd`）と CRT 不一致（LNK2038）でリンクできないため採用していない。
> このため `lib/` は vendor していない。`NETHOST_USE_AS_STATIC` も使用しない。

## Debug / Develop / Release での copy 規則

`nethost.dll` は全構成で実行ファイル横へコピーする。
コピーは `Premake/patch_vcxproj_managed_config.ps1` の PostBuildEvent に集約している
（このスクリプトが Sandbox の PostBuildEvent を上書きするため）。

## 配布方針

- **framework-dependent deployment を正式対応**とする。
  共有 .NET ランタイム（グローバルインストール）を `get_hostfxr_path` のグローバル解決で使用する。
- **self-contained deployment は将来の調査項目**であり、現時点では正式対応と断定しない。
  （`get_hostfxr_parameters.assembly_path` を渡しているため、self-contained 発行で app-local の
  hostfxr / runtime を同梱した場合はそれを優先解決できる素地はあるが、配布検証は未実施。）

## 更新方法

SDK を更新したら、同じ相対パスの `win-x64/native` から `nethost.h` / `hostfxr.h` /
`coreclr_delegates.h` / `nethost.dll` を再コピーし、本 README の version と SHA-256 を更新する。
arm64 / x86 を追加する場合は `bin/<arch>/` を増やし、resolver / copy step を分岐させる。
`get_hostfxr_path` は安定 API のため、ScriptCore の TargetFramework と SDK のメジャー差があっても基本的に動作する。
