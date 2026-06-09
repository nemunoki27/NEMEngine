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

# 09. Native component binding を自動生成する

## 目的

native component の調整可能 parameter をすべて C# へ公開しつつ、手書き callback table の肥大化と追加漏れを防ぐ。

## 前提

- `08_csharp_object_model.md` の `IComponentRef` wrapper を採用する。
- Transform は hot path 専用の batched bridge を残してよい。

## 設計方針

単一の宣言的 binding schema を source of truth にする。例:

```text
Engine/Tools/ManagedBindings/ComponentBindings.json
```

または C++ descriptor DSL でもよい。ただし generated output と手書き schema の責務を明確にする。

schema 例:

```json
{
  "managedName": "SpriteRenderer",
  "nativeType": "Engine::SpriteRendererComponent",
  "componentId": "stable-guid-or-stable-integer",
  "properties": [
    { "name": "texture", "nativeField": "texture", "kind": "AssetRef<TextureAsset>", "access": "ReadWrite" },
    { "name": "visible", "nativeField": "visible", "kind": "Bool", "access": "ReadWrite" }
  ]
}
```

## 生成物

例:

```text
Engine/Core/Scripting/Managed/Generated/GeneratedManagedComponentBindings.h
Engine/Core/Scripting/Managed/Generated/GeneratedManagedComponentBindings.cpp
Engine/Managed/NEM.ScriptCore/Runtime/Generated/GeneratedComponents.g.cs
Engine/Managed/NEM.ScriptCore/Runtime/Generated/GeneratedComponentIds.g.cs
```

`Generated` は手編集禁止コメントを先頭に付ける。

## ABI の拡張方式

property ごとに `ManagedNativeApiTable` の field を増やさない。汎用 typed accessor を使う。

例:

```cpp
ManagedStatus HasComponent(ManagedNativeEntity, uint32_t componentID, int32_t* outHas);
ManagedStatus EnqueueAddComponent(ManagedNativeEntity, uint32_t componentID);
ManagedStatus EnqueueRemoveComponent(ManagedNativeEntity, uint32_t componentID);

ManagedStatus GetBoolProperty(ManagedNativeEntity, uint32_t componentID, uint32_t propertyID, int32_t* out);
ManagedStatus SetBoolProperty(...);
ManagedStatus GetIntProperty(...);
ManagedStatus SetIntProperty(...);
ManagedStatus GetFloatProperty(...);
ManagedStatus SetFloatProperty(...);
ManagedStatus GetVector2Property(...);
ManagedStatus SetVector2Property(...);
ManagedStatus GetVector3Property(...);
ManagedStatus SetVector3Property(...);
ManagedStatus GetColor4Property(...);
ManagedStatus SetColor4Property(...);
ManagedStatus GetAssetRefProperty(...);
ManagedStatus SetAssetRefProperty(...);
```

string、list、nested struct は動的 blob または専用 collection API を使う。

### 性能

- component ID と property ID は build 時定数。
- native 側は `unordered_map<string>` ではなく generated dense table または switch で O(1) dispatch。
- property accessor は entity handle 検証後、`TryGetComponent<T>` で参照。
- string / list 以外は heap allocation なし。
- Transform の高頻度処理は専用 API を優先。

## schema の field 分類

各 field を分類する。

```text
AuthoringReadWrite
RuntimeReadOnly
RuntimeCommand
InternalOnly
```

例:

- `AudioSourceComponent.volume`: AuthoringReadWrite
- `AudioSourceComponent.runtimeVoiceID`: InternalOnly
- `AudioSourceComponent.runtimePlaying`: RuntimeReadOnly
- `CameraControllerComponent.shake.runtimeTime`: InternalOnly
- `SkinnedAnimationComponent.runtimeCurrentClip`: RuntimeReadOnly
- `MeshRendererComponent.subMeshes`: AuthoringReadWrite collection
- runtime matrix: InternalOnly または必要時のみ RuntimeReadOnly

`runtime*` prefix だけで機械的公開しない。schema で明示する。

## collection / nested struct

対象例:

```text
MeshRendererComponent.subMeshes
SubMeshMaterial
CollisionComponent.shapes
CameraControllerComponent.follow/lookAt/shake
BillboardComponent.axes
```

要件:

- nested struct wrapper または value snapshot + commit API。
- list element の取得、更新、追加、削除、並べ替え。
- collection mutation は component の存在確認と index 検証を行う。
- 大きな collection は毎 access で全 JSON serialize しない。
- authoring Inspector と gameplay API で使う経路を分離してもよい。

## generator tool

推奨:

```text
Engine/Tools/NEM.ManagedBindingGenerator/
```

.NET console tool または repository 内で再現可能な generator を使う。

必要 command:

```text
--generate
--verify
```

`--verify` は generated file が schema と一致しない場合に non-zero を返す。CI / build 前確認に使う。

## component 対応範囲

`07_csharp_gameplay_api.md` に列挙した component をすべて schema へ入れる。現時点で `Engine/Core/World/Components` にある component を棚卸しし、除外 field は理由を schema comment または隣接ドキュメントへ書く。

## custom method

単純 property accessor で表現できない操作は手書き extension partial にする。

例:

```text
AudioSource.Play / Stop
SkinnedAnimation.Play / CrossFade / Stop
CameraController.RequestShake
Prefab.Instantiate
SceneManager.LoadAdditive
```

生成 wrapper と手書き partial を分ける。

## project integration

- generator 実行を build step に組み込む。
- generated output が stale なら build failure または自動生成。
- `NEMEngine.vcxproj` と `.filters` を更新する。
- generated `.cs` は ScriptCore project に含める。
- source monitor は generated output を GameScripts source change と誤認しない。

## 回帰テスト

- schema から生成し直して diff が出ない。
- `--verify` が stale output を検出する。
- 各 component の `Has`, `Add`, `Remove`, property get/set が動作する。
- writable 除外した runtime field が C# から書けない。
- SpriteRenderer の通常 property access に managed allocation がない。
- MeshRenderer subMeshes の list 操作が round-trip する。

## 完了チェックリスト

- [ ] binding schema が一つの source of truth。
- [ ] native dispatch と C# wrapper が生成される。
- [ ] hot path が string lookup に依存しない。
- [ ] 全 native component の field を公開 / 非公開理由付きで棚卸しした。
- [ ] custom operation は partial 手書き層に分離した。
- [ ] `--verify` がある。
