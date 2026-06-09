# Claude Code CLI 共通実装ルール

このファイルは、アップロード済みの `Engine.zip` に含まれる NEMEngine の現行コードを前提にした実装指示書です。単なる調査や提案ではなく、記載された完了条件を満たすところまで実装してください。

## 最優先事項

1. 処理速度: gameplay のフレーム更新で不要な reflection、JSON、文字列検索、ヒープ確保を行わない。
2. 拡張性: 新しい component、asset type、scene event、serialized field type を追加しやすい責務分離にする。
3. 汎用性: 特定ゲーム専用の例外処理やハードコードを避ける。
4. 安全性: C++ / C# 境界で use-after-free、ABI 不一致、例外越境、古い handle の誤参照を発生させない。
5. 完成度: TODO、仮実装、空メソッド、将来対応コメントだけを残して「完了」としない。

## 作業方法

- 実装開始前に、対象ファイルと関連コードを読んで現在の設計を把握する。
- 既存 API を変更する場合は、参照箇所をリポジトリ全体で検索し、呼び出し側も同時に更新する。
- 新しい `.h` / `.cpp` を追加した場合は `Engine/NEMEngine.vcxproj` と `Engine/NEMEngine.vcxproj.filters` を更新する。
- 新しい `.cs` を追加した場合は SDK-style project の包含状態を確認する。
- Debug / Develop / Release の差異を意識する。診断機能は Release で無制限にコストを発生させない。
- serialization migration を入れる場合は、旧 scene / prefab の読み込み互換を維持する。
- 仕様上見送る項目は勝手に実装しない。見送る理由と再開条件をコードコメントまたは報告に記載する。

## 作業完了時の報告形式

- 実装した項目
- 変更・追加ファイル一覧
- 互換性のために残した移行処理
- 実行した build / test と結果
- 実行できなかった test と理由
- 未完了項目。原則として 0 件であること。外部要因で不可能な場合のみ、具体的な blocker を記載する。


---

# 11. `nethost` を使って hostfxr 探索を標準化する

## 目的

手動で `C:/Program Files/dotnet/host/fxr` を走査する実装をやめ、.NET 公式 hosting API の `get_hostfxr_path` を利用する。初期化失敗時の cleanup と配布時診断を完成させる。

## 現状の問題

`ManagedScriptRuntime.cpp` の `FindDotnetRoot`, `FindHostfxrPath`, version directory 走査は、環境差、architecture 差、配布構成差に弱い。

## 公式 hosting flow

native host は次の flow にする。

```text
1. nethost の get_hostfxr_path で hostfxr path を得る
2. hostfxr.dll を LoadLibraryW
3. hostfxr_initialize_for_runtime_config
4. hostfxr_get_runtime_delegate
5. hdt_load_assembly_and_get_function_pointer を取得
6. hostfxr_close(context)
7. ScriptCore bridge export を取得
```

## 依存ファイル

公式 hosting header を vendor または取得する。

```text
nethost.h
hostfxr.h
coreclr_delegates.h
nethost.lib / nethost.dll または適切な dynamic loading 構成
```

配置例:

```text
Engine/Externals/dotnet-hosting/include/
Engine/Externals/dotnet-hosting/lib/x64/
```

実際の repository 構成に合わせる。

## 実装方針

### `DotnetHostResolver`

新規 service へ分離する。例:

```text
Engine/Core/Scripting/Managed/DotnetHostResolver.h
Engine/Core/Scripting/Managed/DotnetHostResolver.cpp
```

必要 API:

```cpp
std::filesystem::path ResolveHostfxrPath(const std::filesystem::path& assemblyOrRuntimeConfig);
bool LoadHostfxr(...);
```

### dynamic buffer

`get_hostfxr_path` は必要 buffer size を問い合わせ、dynamic `std::vector<wchar_t>` で再試行する。固定 `MAX_PATH` 前提にしない。

### runtimeconfig

- `NEM.ScriptCore.runtimeconfig.json` の存在を検証。
- framework-dependent deployment と self-contained deployment のどちらを採用するか明文化。
- 開発環境では SDK / runtime 欠落時に明確な error。
- 配布時に runtime 同梱する場合の探索先を config 化。

### RAII

wrapper 例:

```text
UniqueLibraryHandle
UniqueHostfxrContext
```

要件:

- すべての failure path で `FreeLibrary`。
- context は必ず `hostfxr_close`。
- `ManagedScriptRuntime::Finalize()` 複数回呼び出し安全。
- function pointer を library unload 後に使わない。

## architecture 検証

- 現在の project は x64 構成。x64 native host と一致する hostfxr を使う。
- mismatch の場合は「x64 / arm64 / x86」の情報を含む error を出す。
- 将来 architecture 追加時に resolver を差し替えやすくする。

## build project 更新

- `Engine/NEMEngine.vcxproj`
- `Engine/NEMEngine.vcxproj.filters`
- Sandbox executable 側で必要な link / copy step

静的 library 側だけで完結しない場合、最終 executable の linker input と runtime copy も更新する。

## logging

最低限:

```text
runtimeconfig path
resolved hostfxr path
native process architecture
hostfxr initialize result code
runtime delegate result code
ScriptCore assembly path
```

Release では個人環境 path の露出レベルを調整する。

## 公式資料

実装時は Microsoft Learn の custom .NET runtime hosting guide を確認する。

- Custom .NET runtime host: `https://learn.microsoft.com/dotnet/core/tutorials/netcore-hosting`
- Assembly unloadability: `https://learn.microsoft.com/dotnet/standard/assembly/unloadability`

## 回帰テスト

- SDK / runtime が正常な開発機で ScriptCore load 成功。
- `runtimeconfig.json` 欠落で明確な error。
- hostfxr 解決失敗で crash せず終了。
- 初期化途中 failure 後に `Finalize()` しても crash しない。
- path に空白があっても動作。
- x64 構成で x64 hostfxr が選ばれる。

## 完了チェックリスト

- [ ] `FindDotnetRoot` と version directory 手動走査に依存しない。
- [ ] `get_hostfxr_path` を使用する。
- [ ] RAII cleanup がある。
- [ ] runtimeconfig と architecture 診断がある。
- [ ] executable 側の link / copy 設定も確認済み。
