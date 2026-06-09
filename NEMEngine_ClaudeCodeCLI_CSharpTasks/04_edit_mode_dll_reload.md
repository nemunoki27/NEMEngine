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

# 04. Edit モード DLL reload を安全に完成させる

## 目的

GameScripts の編集時 reload を、Editor を固めず、古い DLL に復旧でき、load context leak を診断できる状態にする。

## 非対象

- Play 中 hot reload は実装しない。
- Play 中は source change を記録しても reload しない。Editor に「Stop 後に反映」または「次回 Play 開始時に build」表示を出すだけにする。

## 現状の問題

`ManagedScriptRuntime::AutoRebuildOnScriptChanges()` は、変更検知後に DLL を unload してから `_wsystem(dotnet build ...)` を同期実行する。これにより Editor 停止、復旧性低下、監視ループ、ALC leak の見逃しが起き得る。

## 必須実装 1: reload state machine

`ManagedScriptRuntime` または専用 `ManagedScriptBuildService` に明示状態を持たせる。

```text
Idle
Debouncing
Building
BuildSucceeded
BuildFailed
Staging
ReloadPending
Reloading
ReloadSucceeded
ReloadFailed
```

要件:

- Edit モードの frame tick で state を進める。
- build 中に Editor main thread を block しない。
- 追加変更が来たら dirty flag を立て、現在 build 完了後に再 build する。
- debounce を導入する。推奨 300-750ms の設定値。
- Play 開始操作時は、必要であれば build 完了を明示的に待つかキャンセルする。UI に状態を表示する。

## 必須実装 2: 非同期 build process

Windows の process API で `dotnet build` を子 process として起動し、stdout / stderr を redirect する。

- `_wsystem` を通常 reload 経路から排除する。
- process handle を RAII で管理する。
- build log を逐次 Editor console へ送る。
- exit code を取得する。
- Editor 終了時は process を適切に待つか終了する。
- `DOTNET_CLI_UI_LANGUAGE=en` は process environment block で設定する。
- 引数 quoting を安全にする。

Play 開始前の build は同期完了が必要だが、共通 process runner を使い、UI が応答可能な形にする。

## 必須実装 3: source 監視の除外と debounce

`02_critical_runtime_stabilization.md` の除外を必ず入れる。

推奨構成:

- `std::filesystem` polling を fallback として残す。
- Windows `ReadDirectoryChangesW` または `FileSystemWatcher` 相当を専用 service で使ってもよい。
- 変更 event は path set へ集約する。
- `bin`, `obj`, generated 出力、shadow copy 出力を監視しない。

## 必須実装 4: staging と last-known-good

build 成功前に現在ロード中 assembly を unload しない。

推奨ディレクトリ:

```text
<GameRoot>/Managed/Staging/<profile>/<build-id>/
<GameRoot>/Managed/LastKnownGood/<profile>/
<GameRoot>/Managed/Shadow/<profile>/<reload-id>/
```

手順:

```text
1. source change 検知
2. staging 出力へ dotnet build
3. DLL, PDB, deps.json, runtimeconfig 等の必要物を検証
4. manifest 生成・検証
5. last-known-good を更新可能な状態にする
6. 現在 assembly unload
7. staging から shadow copy を作成
8. shadow copy を collectible ALC へ load
9. type refresh 成功後に active build を切り替える
10. 失敗時は last-known-good の shadow copy を load して復旧
```

要件:

- 実行中 DLL を直接上書きしない。
- build failure では現在の正常 DLL を維持する。
- reload failure でも復旧を試みる。
- 古い shadow directory は保持数を決めて掃除する。
- Play 中は active assembly を差し替えない。

## 必須実装 5: ALC unload 診断

C# `HostBridge.ReleaseGameAssembly` を改善する。

- unload 対象 `GameScriptLoadContext` の `WeakReference` を作る。
- ALC への strong reference が scope 外へ出る構造にする。
- `Unload()` 後に限定回数の `GC.Collect()`, `GC.WaitForPendingFinalizers()` を行う。
- weak reference が残る場合は warning を出す。
- warning には reload 回数、load context 名、疑わしい原因を含める。
- GC loop は無制限に回さない。

## 必須実装 6: managed lifetime cancellation

古い GameScripts assembly を参照し続ける task、event、timer を止める。

ScriptCore 側に assembly lifetime service を追加する。例:

```csharp
public static class ScriptRuntimeLifetime {
    public static CancellationToken reloadToken { get; }
    internal static void BeginAssemblyLifetime();
    internal static void EndAssemblyLifetime();
    public static IDisposable Register(IDisposable disposable);
}
```

要件:

- unload 前に cancellation token を cancel。
- ScriptCore が所有する coroutine / timer を停止。
- 登録済み `IDisposable` を dispose。
- static event subscription を登録解除する helper を用意する。
- user code が追跡外 static reference を残した場合、ALC leak warning で検知する。

## 必須実装 7: native dependency resolver

`GameScriptLoadContext` に `LoadUnmanagedDll` override を追加する。

- `AssemblyDependencyResolver.ResolveUnmanagedDllToPath` を使う。
- path が解決できた場合だけ `LoadUnmanagedDllFromPath`。
- managed dependency の既存 `Load()` と同じ責務で扱う。
- ScriptCore 本体は default context の assembly を共有する。

## 必須実装 8: reload diagnostics

Editor console に以下を構造化して出す。

```text
build id
reload id
changed source path count
build duration
load duration
script type count
manifest validation result
ALC unload success / failure
fallback 実施有無
```

`12_editor_scripting_tooling.md` の Compiler Error List と統合する。

## 変更候補ファイル

```text
Engine/Core/Scripting/Managed/ManagedScriptRuntime.*
Engine/Core/Scripting/Managed/ManagedScriptBuildService.*       (new 推奨)
Engine/Core/Scripting/Managed/ManagedProcessRunner.*            (new 推奨)
Engine/Core/Runtime/Application/EngineApplication.cpp
Engine/Managed/NEM.ScriptCore/Runtime/HostBridge.cs
Engine/Managed/NEM.ScriptCore/Runtime/ScriptRuntimeLifetime.cs  (new)
Engine/Editor/UI/Panels/Builtin/ConsolePanel.*
```

## 回帰テスト

- `.cs` を連続保存しても build storm が起きない。
- `obj` 更新で rebuild が再帰発火しない。
- syntax error を入れても直前の正常 DLL と Inspector schema が維持される。
- 正常化して保存すると reload できる。
- 100 回 reload して ALC が回収される。
- 意図的に static event reference を残すと leak warning が出る。
- native dependency を持つ test assembly が resolver 経由で load できる。
- Play 中の保存では reload されず、Stop 後または次 Play 前に反映される。

## 完了チェックリスト

- [ ] `_wsystem` による通常 Editor block がない。
- [ ] build 前 unload を行わない。
- [ ] staging / shadow copy / last-known-good が動作する。
- [ ] source monitor に除外と debounce がある。
- [ ] ALC leak を検出できる。
- [ ] reload lifetime cancellation がある。
- [ ] Play 中 reload が無効である。
