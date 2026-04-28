# Collision

## 概要

Collisionは、Entityに `CollisionComponent` を追加して衝突タイプと衝突形状を設定し、Play中に `CollisionSystem` が判定、押し戻し、衝突コールバックを行う機能。

今回の作業では機能追加を止め、既存実装に見えるようにコメント、ヘッダ説明、変数名、関数説明を整理した。

## 主なファイル

- `Project/Engine/Core/Collision/CollisionTypes.h/.cpp`
  - 衝突形状、衝突タイプ、接触情報、Entityペアキー、文字列変換を定義する。
- `Project/Engine/Core/Collision/CollisionSettings.h/.cpp`
  - Collisionタイプと衝突マトリクスを管理、保存、読み込みする。
- `Project/Engine/Core/Collision/CollisionDetection.h/.cpp`
  - Transform反映後の形状同士の衝突判定を行う。
- `Project/Engine/Core/ECS/Component/Builtin/CollisionComponent.h/.cpp`
  - Entityに付けるCollision設定を持つ。
- `Project/Engine/Core/ECS/System/Builtin/CollisionSystem.h/.cpp`
  - Play中の衝突判定、押し戻し、コールバック発行を行う。
- `Project/Engine/Editor/Inspector/Methods/CollisionInspectorDrawer.h/.cpp`
  - Inspector上で `CollisionComponent` を編集する。
- `Project/Engine/Editor/Tools/CollisionManagerTool.h/.cpp`
  - ToolPanelから開くCollisionManagerウィンドウを実装する。

## 使い方

1. EntityへInspectorから `Collision` コンポーネントを追加する。
2. `Collision Types` でCollisionManagerに登録済みのタイプを選ぶ。複数選択できる。
3. `Shapes` に衝突形状を追加する。1つのEntityに複数形状を持たせられる。
4. ToolPanelの `CollisionManager` を開き、CollisionタイプとLayer Collision Matrixを編集する。
5. Play中に衝突すると、C++ `MonoBehavior` またはC# `ScriptBehaviour` の `OnCollisionEnter/Stay/Exit` が呼ばれる。

## 衝突形状

- `Circle2D`
  - XY平面上の円。
  - `radius` と `offset` を使用する。
- `Quad2D`
  - XY平面上の矩形。
  - `halfSize2D` を使用する。
  - `rotatedQuad` がtrueの場合は形状回転を反映する。
- `Sphere3D`
  - 球。
  - `radius` と `offset` を使用する。
- `AABB3D`
  - ワールド軸に固定されたBox。
  - `halfExtents3D` を使用する。
- `OBB3D`
  - 回転ありのBox。
  - `halfExtents3D`、`rotationDegrees`、必要に応じてTransform回転を使用する。

## CollisionManager

- `Collision Types`
  - Collisionタイプ名と有効状態を編集する。
  - タイプ追加時は、既存タイプすべてと衝突する設定で追加される。
  - タイプ削除は最後のタイプから行う。
- `Layer Collision Matrix`
  - `ImGui::Table` を使い、タイプ同士の衝突可否をチェックボックスで編集する。
  - 無効なタイプ、またはMatrixで無効な組み合わせは判定されない。
- `DrawCollisionWorld`
  - 有効にすると、World内のCollision形状を `LineRenderer` で描画する。
  - Trigger形状は黄色、通常形状はシアンで表示する。
  - `_DEBUG` または `_DEVELOPBUILD` のときだけ描画する。

## コールバック

### C++

```cpp
void OnCollisionEnter(ECSWorld& world, const SystemContext& context, const CollisionContact& collision) override;
void OnCollisionStay(ECSWorld& world, const SystemContext& context, const CollisionContact& collision) override;
void OnCollisionExit(ECSWorld& world, const SystemContext& context, const CollisionContact& collision) override;
```

### C#

```csharp
public override void OnCollisionEnter(Collision collision) {}
public override void OnCollisionStay(Collision collision) {}
public override void OnCollisionExit(Collision collision) {}
```

`Collision` には `self`、`entity`、`normal`、`point`、`penetration`、`selfShapeIndex`、`otherShapeIndex`、`isTrigger` が入る。

## リファクタ内容

- Collision関連ヘッダに、既存コードと同じ区切りコメントを追加。
- public/private関数へ説明コメントを追加。
- 構造体フィールドへ用途コメントを追加。
- `CollisionSettings::SetPairEnabled`、`IsPairEnabled`、`CanCollide` の引数名を分かりやすく変更。
- `CollisionDetection`、`CollisionSystem`、`CollisionManagerTool` の処理ブロックへ短い説明コメントを追加。
- ManagedScript側のCollisionイベント経路にも、必要なフィールド説明と処理コメントを追加。

## 検証

- `dotnet build Project\Engine\Managed\NEM.ScriptCore\NEM.ScriptCore.csproj -c Debug --nologo`
  - 成功、警告0、エラー0。
- `MSBuild Project\NEMEngine.slnx /t:Sandbox /p:Configuration=Debug /p:Platform=x64 /m`
  - 成功、警告0、エラー0。
- `git diff --check`
  - 空白エラーなし。改行コード警告のみ。
