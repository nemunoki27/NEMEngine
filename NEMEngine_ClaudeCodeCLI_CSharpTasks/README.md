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

# NEMEngine C# スクリプト基盤 実装指示書セット

このフォルダーには、ユーザーが指定した `・` 区切りごとの Claude Code CLI 用実装指示書が入っています。各ファイルは単独でも読めますが、依存関係があるため原則として以下の順序で実装してください。

## 実装順

| 順番 | ファイル | 主目的 |
|---:|---|---|
| 1 | `02_critical_runtime_stabilization.md` | use-after-free、ABI、例外越境、構造変更、初期化失敗を先に修正 |
| 2 | `10_managed_instance_handle_generation.md` | managed instance handle の世代管理を完成 |
| 3 | `03_lifecycle_multi_pass.md` | lifecycle を複数 pass 化し、inactive hierarchy の Awake を遅延 |
| 4 | `11_nethost_hostfxr_standardization.md` | hostfxr 探索を標準化し、失敗経路を RAII 化 |
| 5 | `04_edit_mode_dll_reload.md` | Edit モード DLL reload を安全・非同期・復旧可能にする |
| 6 | `06_script_manifest_stable_identity.md` | script type の安定 GUID と manifest を導入 |
| 7 | `05_inspector_serialization.md` | serializer、属性、参照型、動的バッファ、runtime inspector を実装 |
| 8 | `08_csharp_object_model.md` | Entity / Component / ScriptBehaviour の公開モデルを確定 |
| 9 | `09_component_binding_codegen.md` | native component binding の自動生成を実装 |
| 10 | `07_csharp_gameplay_api.md` | Transform、component、Prefab、Scene、Asset、Input、Time、Coroutine を実装 |
| 11 | `12_editor_scripting_tooling.md` | Editor の診断、Missing Script、Profiler、テンプレート等を完成 |
| 12 | `13_full_game_readiness_scope.md` | 全体統合、未実装検出、縦切りテスト、完成判定 |

## 今回の明示的な非対象

- Play 中の GameScripts DLL ホットリロードは実装しない。Edit モード中の reload と Play 開始前 build / reload のみを対象にする。
- 新しい Rigidbody、Raycast、Overlap 系 Physics API は、native physics 基盤が整うまで実装しない。既存 collision callback と既存 `CollisionComponent` の binding は維持・整理する。

## 現行コードで確認済みの主要な起点

- Native managed runtime: `Engine/Core/Scripting/Managed/ManagedScriptRuntime.*`
- C# bridge: `Engine/Managed/NEM.ScriptCore/Runtime/HostBridge.cs`, `NativeApi.cs`
- Script lifecycle: `Engine/Core/World/Systems/Behavior/BehaviorSystem.*`
- Native behavior store: `Engine/Core/World/Behavior/World/BehaviorWorld.*`
- ECS structural change: `Engine/Core/World/ECS/World/ECSWorld.*`
- Play / Edit switch: `Engine/Core/Runtime/Application/EngineApplication.cpp`
- Script inspector: `Engine/Editor/UI/Inspectors/Builtin/ScriptInspectorDrawer.*`
- Script drag & drop: `Engine/Editor/Scripting/DragDrop/ScriptAssetDragDrop.*`

## 完成の定義

各指示書のチェックリストがすべて満たされ、`13_full_game_readiness_scope.md` の統合テストを通過した時点で完了です。個別ファイルを実装しただけでは完了としません。
