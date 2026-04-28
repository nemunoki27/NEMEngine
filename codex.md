# Codex Work Log

## 2026-04-28 Collision実装整理

### 作業概要
- Collision機能の新規実装はいったん終了し、既存コードに寄せるためのリファクタを実施。
- `CollisionSettings` を中心に、Collision関連のヘッダへ関数説明、変数説明、区切りコメントを追加。
- `.cpp` 側へ処理単位の短いコメントを追加し、既存実装で使われている `classMethods`、`public Methods`、`private Methods`、`variables`、`functions` の書き方に寄せた。
- 公開関数の意味が分かりづらかった引数名を、`typeA`、`typeB`、`typeMaskA`、`typeMaskB` などに整理。

### 対象ファイル
- `Project/Engine/Core/Collision/CollisionTypes.h/.cpp`
- `Project/Engine/Core/Collision/CollisionSettings.h/.cpp`
- `Project/Engine/Core/Collision/CollisionDetection.h/.cpp`
- `Project/Engine/Core/ECS/Component/Builtin/CollisionComponent.h/.cpp`
- `Project/Engine/Core/ECS/System/Builtin/CollisionSystem.h/.cpp`
- `Project/Engine/Editor/Inspector/Methods/CollisionInspectorDrawer.h/.cpp`
- `Project/Engine/Editor/Tools/CollisionManagerTool.h/.cpp`
- `Project/Engine/Core/ECS/Behavior/MonoBehavior.h`
- `Project/Engine/Core/ECS/System/Builtin/BehaviorSystem.h/.cpp`
- `Project/Engine/Core/Scripting/ManagedBehavior.h/.cpp`
- `Project/Engine/Core/Scripting/ManagedScriptRuntime.h/.cpp`
- `Project/Engine/Core/Scripting/ManagedScriptTypes.h`
- `Project/Engine/Managed/NEM.ScriptCore/Runtime/HostBridge.cs`
- `Project/Engine/Managed/NEM.ScriptCore/Runtime/ScriptBehaviour.cs`

### Collision機能の現在の構成
- `CollisionComponent` をEntityへ追加すると、Collisionタイプと複数の衝突形状を設定できる。
- 2D形状は `Circle2D`、`Quad2D` に対応。`Quad2D` は回転なし、回転ありの両方を扱う。
- 3D形状は `Sphere3D`、`AABB3D`、`OBB3D` に対応。
- `CollisionSettings` がCollisionタイプ一覧と衝突マトリクスを管理し、`GameAssets/Collision/collisionSettings.json` に保存する。
- `CollisionSystem` がPlay中に判定、押し戻し、`OnCollisionEnter/Stay/Exit` の発行を行う。
- C++ `MonoBehavior` とC# `ScriptBehaviour` の両方で `OnCollisionEnter`、`OnCollisionStay`、`OnCollisionExit` を使える。
- `CollisionManagerTool` はToolPanelから開ける `CollisionManager` ウィンドウで、CollisionタイプとLayer Collision Matrix相当の設定を編集する。
- `DrawCollisionWorld` が有効な場合、Entityに設定されたCollision形状を `LineRenderer` でデバッグ描画する。

### 検証
- `dotnet build Project\Engine\Managed\NEM.ScriptCore\NEM.ScriptCore.csproj -c Debug --nologo`
  - 成功、警告0、エラー0。
- `MSBuild Project\NEMEngine.slnx /t:Sandbox /p:Configuration=Debug /p:Platform=x64 /m`
  - 成功、警告0、エラー0。
- `git diff --check`
  - 空白エラーなし。既存の改行コード警告のみ。
