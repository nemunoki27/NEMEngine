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

# 06. Script type 識別をファイル名依存から stable manifest へ移行する

## 目的

`.cs` ファイル名、単純 class 名、namespace に依存せず script を識別する。class rename、namespace rename、ファイル移動、1 ファイル複数 class に耐える。

## 現状の問題

`Engine/Editor/Scripting/DragDrop/ScriptAssetDragDrop.cpp` は script asset path の stem を class 名として解決する。`ScriptEntry` は `std::string type` を保存主キーとしている。

## Stable Script Type ID

各 concrete `ScriptBehaviour` に UUID を持たせる。

```csharp
[ScriptTypeId("0d2f...uuid")]
public sealed class PlayerController : ScriptBehaviour {
}
```

要件:

- ID は rename、namespace change、file move で不変。
- script template 作成時に UUID を自動生成する。
- 1 ファイルに複数 concrete script があれば各 type が別 ID を持つ。
- ID 重複は build error。
- attribute がない script は Editor で明示 warning。移行期間のみ deterministic fallback ID を生成可能だが、shipping / completion 時には全 concrete script に明示 ID があること。

## Script Manifest

build 後に manifest を生成する。

```json
{
  "manifestVersion": 1,
  "assembly": "GameScripts",
  "buildId": "...",
  "scripts": [
    {
      "scriptTypeId": "...",
      "fullTypeName": "Game.Player.PlayerController",
      "displayName": "Player Controller",
      "sourcePath": "Scripts/PlayerController.cs",
      "sourceAssetId": "...",
      "schemaVersion": 2,
      "defaultExecutionOrder": 0
    }
  ]
}
```

## 推奨生成方式

Roslyn source generator を追加する。

例:

```text
Engine/Managed/NEM.ScriptCodeGen/
Sandbox/Scripts/GameScripts.csproj から Analyzer として参照
```

generator は compile 時に concrete `ScriptBehaviour` を列挙し、生成 C# registry を出す。

```csharp
internal static partial class GeneratedScriptManifest {
    internal static ReadOnlySpan<ScriptTypeDescriptor> descriptors => ...;
}
```

`HostBridge` は reflection 全走査ではなく、生成 registry を優先して読む。開発移行用 fallback reflection は warning 付きで限定的に残してよい。

build service は registry から `GameScripts.manifest.json` を staging へ出力し、native 側が load 前後に検証する。

## `ScriptEntry` の変更

保存主キーを stable ID にする。

```cpp
struct ScriptEntry {
    UUID scriptSlotID{};
    UUID scriptTypeID{};
    AssetID scriptAsset{};
    std::string lastKnownTypeName;
    bool enabled = true;
    nlohmann::json serializedFields = nlohmann::json::object();

    // runtime cache only
    BehaviorHandle handle = BehaviorHandle::Null();
    uint32_t resolvedRuntimeTypeID = 0;
    bool resolvedRuntimeTypeValid = false;
};
```

要件:

- `type` 文字列は保存主キーとして使わない。
- display と legacy migration 用に last-known name を保存してよい。
- `scriptSlotID` は同じ entity に同 type を複数 attach しても識別できる stable UUID。
- runtime type ID は reload ごとに変わってよい。scene / prefab に保存しない。

## Registry

`BehaviorTypeRegistry` を拡張する。

必要検索:

```text
FindByStableScriptTypeID(UUID)
FindManagedByFullName(string)          // legacy migration / display only
FindManagedBySimpleName(string)        // legacy migration only。曖昧なら失敗
FindBySourceAsset(AssetID)             // drag drop picker 用
```

要件:

- runtime hot path は UUID または数値 ID で解決する。
- simple name が複数候補なら自動選択しない。
- manifest validation failure は明示ログ。

## Drag & Drop

`ScriptAssetDragDrop` は asset path stem を使わない。

- script asset ID -> manifest の source asset mapping で候補 type を得る。
- 候補 1 件なら attach。
- 複数件なら picker popup を出す。
- 0 件なら「このファイルに attach 可能な ScriptBehaviour がない」と表示。
- manifest load 不可なら build error 状態を表示。

## Legacy migration

旧 scene / prefab の `type` 文字列を読み込む。

```text
1. full name 一致
2. 単純名が一意なら一致
3. source script asset mapping が一意なら一致
4. 解決不能なら Missing Script としてデータ保持
```

migration 後保存時に stable type ID を書く。

## Missing Script

解決不能でも entry を削除しない。

表示情報:

```text
last-known type name
script asset ID
script type ID
script slot ID
serialized field JSON
解決失敗理由
```

再び script が存在するようになったら自動復旧可能にする。

## 変更候補ファイル

```text
Engine/Core/World/Components/Scripting/ScriptComponent.*
Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.*
Engine/Core/Scripting/Managed/ManagedScriptRuntime.*
Engine/Managed/NEM.ScriptCore/Runtime/HostBridge.cs
Engine/Managed/NEM.ScriptCore/Runtime/Attributes/ScriptTypeIdAttribute.cs       (new)
Engine/Managed/NEM.ScriptCore/Runtime/Metadata/ScriptTypeDescriptor.cs         (new)
Engine/Managed/NEM.ScriptCodeGen/*                                             (new)
Engine/Editor/Scripting/DragDrop/ScriptAssetDragDrop.*
Engine/Editor/UI/Inspectors/Builtin/ScriptInspectorDrawer.*
```

## 回帰テスト

- class rename 後も既存 scene の script が維持される。
- namespace rename 後も維持される。
- `.cs` file move と file rename 後も維持される。
- 1 file に 2 script class がある場合、drag drop で picker が出る。
- 同一 simple name が別 namespace にある場合、legacy simple-name migration が曖昧エラーになる。
- ScriptTypeId 重複で build error。
- script 削除後に Missing Script と値が残り、復元後に再接続される。

## 完了チェックリスト

- [ ] scene / prefab の保存主キーが stable script type ID。
- [ ] drag drop が filename stem に依存しない。
- [ ] script slot ID がある。
- [ ] manifest が staging build と一緒に生成・検証される。
- [ ] Missing Script が値を保持する。
- [ ] legacy scene migration がある。
