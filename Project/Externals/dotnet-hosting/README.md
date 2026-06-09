# dotnet-hosting (vendored .NET native hosting files)

`nethost` / `hostfxr` の公式 native hosting ヘッダと静的ライブラリを vendor したもの。
`ManagedScriptRuntime` が `get_hostfxr_path()` で hostfxr を解決するために使用する。

## 取得元

.NET SDK の host pack（インストール済み SDK に同梱）からコピーした。

```
%ProgramFiles%\dotnet\packs\Microsoft.NETCore.App.Host.win-x64\10.0.8\runtimes\win-x64\native\
  nethost.h
  hostfxr.h
  coreclr_delegates.h
  libnethost.lib   (静的ライブラリ。nethost.dll の配布が不要)
```

ライセンスは MIT（各ヘッダ先頭の表記参照）。

## 構成

```
include/   nethost.h / hostfxr.h / coreclr_delegates.h
lib/x64/   libnethost.lib   （静的。NETHOST_USE_AS_STATIC で取り込む）
```

## 更新方法

SDK を更新したら、同じ相対パスの `win-x64/native` から上記4ファイルを再コピーする。
`get_hostfxr_path` は安定 API のため、ScriptCore の TargetFramework と SDK のメジャー差があっても基本的に動作する。
arm64 / x86 を追加する場合は `lib/<arch>/` を増やし、Premake の `libdirs` を分岐させる。
