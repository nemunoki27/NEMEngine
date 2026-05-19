# NEMEngine02 `Engine/Core` フォルダ再編 実装指示書 for Codex CLI

対象: C++ / DirectX12 自作ゲームエンジン `NEMEngine_02`

目的: `Engine/Core/` 配下を、機能・責務別に読みやすく再分類する。大幅変更可。既存コードの動作は維持し、フォルダ名・一部ファイル名も役割が分かる名前にRenameする。

---

## 0. 最重要ルール

1. **動作を変えない。** 今回は構造整理が目的。クラス設計や処理内容の変更は最小限にする。
2. **namespace は原則変更しない。** 物理パス変更に伴う include / project file 更新だけを行う。namespace変更は後続タスクに分離する。
3. **Visual Studioプロジェクトを必ず更新する。** `Engine/NEMEngine.vcxproj` と `Engine/NEMEngine.vcxproj.filters` の `ClCompile` / `ClInclude` / `Filter` を新パスへ更新する。
4. **includeはコンパイルが通る形へ更新する。** 基本方針は、同一ディレクトリの自己ヘッダは `#include "Foo.h"` のまま、それ以外は新しい `Core/...` ルート基準の include に直す。
5. **ファイル移動後、旧ディレクトリは空なら削除する。**
6. **Windows環境前提。** パス区切りは `.vcxproj` では `\`、C++ include では `/` を使う。
7. **Assets / Editor / Managed / Library は今回対象外。** 変更対象は基本的に `Engine/Core` と、その参照更新のみ。

---

## 1. 新しい大分類

`Engine/Core` を以下の7カテゴリに整理する。

```txt
Engine/Core/
  Foundation/      # 数学、ID、ログ、時刻、汎用Utility、Build設定など。最も低依存。
  Runtime/         # EngineApplication / Framework / EngineContext / RuntimePaths など実行基盤。
  Platform/        # OS窓、入力などプラットフォーム境界。
  Assets/          # AssetDatabase / AssetTypes / AssetWorkerPool など。
  World/           # ECS / Scene / Prefab / Behavior などゲームワールド表現。
  Rendering/       # 旧 Graphics。DirectX12 RHI、GPUリソース、パイプライン、描画機能。
  Scripting/       # Managed runtime / behavior bridge。
  Animation/       # AnimationClip / Curve / Evaluator。
  Physics/         # 旧 Collision。現状はCollision中心。
  Audio/           # Audio module。
  Tools/           # Editor/Tool APIに近い共通ツール基盤。
```

> 備考: `Scripting/Animation/Physics/Audio/Tools` は将来独立モジュール化しやすいよう、`World` や `Runtime` に吸収しない。

---

## 2. 移動・Renameマッピング

### 2.1 Core直下ファイル

```txt
Engine/Core/Framework.h   -> Engine/Core/Runtime/Framework/EngineFramework.h
Engine/Core/Framework.cpp -> Engine/Core/Runtime/Framework/EngineFramework.cpp
```

ファイル名変更後、クラス名が `Framework` のままならクラス名は変更しなくてよい。ただし include は `EngineFramework.h` に合わせる。

---

### 2.2 Foundation

```txt
Engine/Core/Build/             -> Engine/Core/Foundation/Build/
Engine/Core/Logger/            -> Engine/Core/Foundation/Diagnostics/
Engine/Core/Time/              -> Engine/Core/Foundation/Time/
Engine/Core/UUID/              -> Engine/Core/Foundation/Identity/
Engine/Core/Math/              -> Engine/Core/Foundation/Math/
Engine/Core/Utility/Algorithm/ -> Engine/Core/Foundation/Utility/Algorithm/
Engine/Core/Utility/Command/   -> Engine/Core/Foundation/Utility/Command/
Engine/Core/Utility/Enum/      -> Engine/Core/Foundation/Utility/Enum/
Engine/Core/Utility/Json/      -> Engine/Core/Foundation/Serialization/Json/
Engine/Core/Utility/Random/    -> Engine/Core/Foundation/Utility/Random/
Engine/Core/Utility/ImGui/     -> Engine/Core/Tools/ImGui/
```

#### ファイルRename

```txt
Engine/Core/Foundation/Diagnostics/Logger.h       -> Engine/Core/Foundation/Diagnostics/Log.h
Engine/Core/Foundation/Diagnostics/Logger.cpp     -> Engine/Core/Foundation/Diagnostics/Log.cpp
Engine/Core/Foundation/Identity/UUID.h            -> Engine/Core/Foundation/Identity/Uuid.h
Engine/Core/Foundation/Identity/UUID.cpp          -> Engine/Core/Foundation/Identity/Uuid.cpp
Engine/Core/Foundation/Identity/TypeHash.h        -> Engine/Core/Foundation/Identity/TypeId.h
Engine/Core/Foundation/Identity/TypeHash.cpp      -> Engine/Core/Foundation/Identity/TypeId.cpp
Engine/Core/Foundation/Serialization/Json/JsonAdapter.h   -> Engine/Core/Foundation/Serialization/Json/JsonSerializer.h
Engine/Core/Foundation/Serialization/Json/JsonAdapter.cpp -> Engine/Core/Foundation/Serialization/Json/JsonSerializer.cpp
Engine/Core/Tools/ImGui/MyGUI.h                   -> Engine/Core/Tools/ImGui/ImGuiHelpers.h
Engine/Core/Tools/ImGui/MyGUI.cpp                 -> Engine/Core/Tools/ImGui/ImGuiHelpers.cpp
Engine/Core/Tools/ImGui/MyGUICurve.cpp            -> Engine/Core/Tools/ImGui/ImGuiCurveEditor.cpp
```

`MyGUICurve.cpp` に対応するヘッダが無い場合は新規ヘッダを作らない。cpp名のみ変更する。

---

### 2.3 Runtime / Platform

```txt
Engine/Core/Runtime/EngineApplication.* -> Engine/Core/Runtime/Application/EngineApplication.*
Engine/Core/Runtime/RuntimePaths.*      -> Engine/Core/Runtime/Paths/RuntimePaths.*
Engine/Core/Context/EngineContext.*     -> Engine/Core/Runtime/Context/EngineContext.*
Engine/Core/Window/WinApp.*             -> Engine/Core/Platform/Windows/Win32Window.*
Engine/Core/Input/                      -> Engine/Core/Platform/Input/
Engine/Core/Input/Base/                 -> Engine/Core/Platform/Input/Devices/
```

#### ファイルRename

```txt
Engine/Core/Platform/Input/Input.h              -> Engine/Core/Platform/Input/InputSystem.h
Engine/Core/Platform/Input/Input.cpp            -> Engine/Core/Platform/Input/InputSystem.cpp
Engine/Core/Platform/Input/InputStructures.h    -> Engine/Core/Platform/Input/InputTypes.h
Engine/Core/Platform/Input/InputStructures.cpp  -> Engine/Core/Platform/Input/InputTypes.cpp
Engine/Core/Platform/Input/Devices/IInputDevice.h -> Engine/Core/Platform/Input/Devices/InputDevice.h
Engine/Core/Platform/Input/Devices/InputMapper.h  -> Engine/Core/Platform/Input/InputMapper.h
```

`WinApp` クラス名は今回は変更しない。ファイル名だけ `Win32Window` に変える。

---

### 2.4 Assets

```txt
Engine/Core/Asset/AssetDatabase.*       -> Engine/Core/Assets/Database/AssetDatabase.*
Engine/Core/Asset/AssetTypes.*          -> Engine/Core/Assets/AssetTypes.*
Engine/Core/Asset/Async/AssetWorkerPool.h -> Engine/Core/Assets/Async/AssetWorkerPool.h
```

---

### 2.5 World: ECS / Scene / Prefab / Behavior

```txt
Engine/Core/ECS/Entity/                 -> Engine/Core/World/ECS/Entity/
Engine/Core/ECS/World/                  -> Engine/Core/World/ECS/World/
Engine/Core/ECS/Config/                 -> Engine/Core/World/ECS/Config/
Engine/Core/ECS/Component/Type/         -> Engine/Core/World/ECS/Components/Core/
Engine/Core/ECS/Component/Registry/     -> Engine/Core/World/ECS/Components/Registry/
Engine/Core/ECS/Component/Builtin/      -> Engine/Core/World/Components/
Engine/Core/ECS/System/Interface/       -> Engine/Core/World/ECS/Systems/Core/
Engine/Core/ECS/System/Context/         -> Engine/Core/World/ECS/Systems/Context/
Engine/Core/ECS/System/Scheduler/       -> Engine/Core/World/ECS/Systems/Scheduler/
Engine/Core/ECS/System/Builtin/         -> Engine/Core/World/Systems/
Engine/Core/ECS/Behavior/               -> Engine/Core/World/Behavior/
Engine/Core/Scene/                      -> Engine/Core/World/Scene/
Engine/Core/Prefab/                     -> Engine/Core/World/Prefab/
```

#### World配下の整理後イメージ

```txt
Engine/Core/World/
  ECS/
    Config/
    Entity/
    World/
    Components/
      Core/
      Registry/
    Systems/
      Core/
      Context/
      Scheduler/
  Components/
    Transform/
    Camera/
    Lighting/
    Rendering/
    Animation/
    Audio/
    Physics/
    Scripting/
    Scene/
    Prefab/
  Systems/
    Transform/
    Camera/
    Rendering/
    Animation/
    Audio/
    Physics/
    Behavior/
    Hierarchy/
  Behavior/
    Core/
    Registry/
    World/
  Scene/
    Core/
    Serialization/
    Runtime/
  Prefab/
    Core/
    Serialization/
```

#### Builtin Component の詳細移動

```txt
NameComponent.*             -> Engine/Core/World/Components/Scene/NameComponent.*
SceneObjectComponent.*      -> Engine/Core/World/Components/Scene/SceneObjectComponent.*
HierarchyComponent.*        -> Engine/Core/World/Components/Transform/HierarchyComponent.*
TransformComponent.*        -> Engine/Core/World/Components/Transform/TransformComponent.*
UVTransformComponent.*      -> Engine/Core/World/Components/Rendering/UVTransformComponent.*
CameraComponent.*           -> Engine/Core/World/Components/Camera/CameraComponent.*
CameraControllerComponent.* -> Engine/Core/World/Components/Camera/CameraControllerComponent.*
AudioSourceComponent.*      -> Engine/Core/World/Components/Audio/AudioSourceComponent.*
CollisionComponent.*        -> Engine/Core/World/Components/Physics/CollisionComponent.*
ScriptComponent.*           -> Engine/Core/World/Components/Scripting/ScriptComponent.*
PrefabLinkComponent.*       -> Engine/Core/World/Components/Prefab/PrefabLinkComponent.*
Light/*. *                  -> Engine/Core/World/Components/Lighting/
Render/*. *                 -> Engine/Core/World/Components/Rendering/
Animation/*. *              -> Engine/Core/World/Components/Animation/
```

#### Builtin System の詳細移動

```txt
TransformUpdateSystem.*         -> Engine/Core/World/Systems/Transform/TransformSystem.*
UVTransformUpdateSystem.*       -> Engine/Core/World/Systems/Rendering/UVTransformSystem.*
CameraControllerSystem.*        -> Engine/Core/World/Systems/Camera/CameraControllerSystem.*
AudioSourceSystem.*             -> Engine/Core/World/Systems/Audio/AudioSourceSystem.*
CollisionSystem.*               -> Engine/Core/World/Systems/Physics/CollisionSystem.*
BehaviorSystem.*                -> Engine/Core/World/Systems/Behavior/BehaviorSystem.*
HierarchySystem.*               -> Engine/Core/World/Systems/Hierarchy/HierarchySystem.*
SkinnedAnimationUpdateSystem.*  -> Engine/Core/World/Systems/Animation/SkinnedAnimationSystem.*
```

#### Scene / Prefab の整理

```txt
Engine/Core/World/Scene/Header/SceneHeader.*      -> Engine/Core/World/Scene/Serialization/SceneHeader.*
Engine/Core/World/Scene/Instance/SceneInstanceManager.* -> Engine/Core/World/Scene/Runtime/SceneInstanceManager.*
Engine/Core/World/Scene/SceneAuthoring.*          -> Engine/Core/World/Scene/Authoring/SceneAuthoring.*
Engine/Core/World/Scene/SceneSystem.*             -> Engine/Core/World/Scene/Runtime/SceneSystem.*
Engine/Core/World/Prefab/Header/PrefabHeader.*    -> Engine/Core/World/Prefab/Serialization/PrefabHeader.*
Engine/Core/World/Prefab/PrefabSystem.*           -> Engine/Core/World/Prefab/Runtime/PrefabSystem.*
```

---

### 2.6 Animation

```txt
Engine/Core/Animation/AnimationClipEvaluator.* -> Engine/Core/Animation/Evaluation/AnimationClipEvaluator.*
Engine/Core/Animation/Clip/                   -> Engine/Core/Animation/Clips/
Engine/Core/Animation/Curve/                  -> Engine/Core/Animation/Curves/
Engine/Core/Animation/Property/               -> Engine/Core/Animation/Properties/
```

---

### 2.7 Physics

```txt
Engine/Core/Collision/CollisionDetection.* -> Engine/Core/Physics/Collision/CollisionDetection.*
Engine/Core/Collision/CollisionSettings.*  -> Engine/Core/Physics/Collision/CollisionSettings.*
Engine/Core/Collision/CollisionTypes.*     -> Engine/Core/Physics/Collision/CollisionTypes.*
```

---

### 2.8 Audio / Scripting / Tools

```txt
Engine/Core/Audio/Audio.* -> Engine/Core/Audio/AudioSystem.*

Engine/Core/Scripting/ManagedBehavior.*      -> Engine/Core/Scripting/Managed/ManagedBehavior.*
Engine/Core/Scripting/ManagedScriptRuntime.* -> Engine/Core/Scripting/Managed/ManagedScriptRuntime.*
Engine/Core/Scripting/ManagedScriptTypes.h   -> Engine/Core/Scripting/Managed/ManagedScriptTypes.h

Engine/Core/Tools/ITool.*        -> Engine/Core/Tools/Core/ITool.*
Engine/Core/Tools/ToolAPI.h      -> Engine/Core/Tools/Core/ToolAPI.h
Engine/Core/Tools/ToolRegistry.* -> Engine/Core/Tools/Registry/ToolRegistry.*
Engine/Core/Tools/ToolTypes.h    -> Engine/Core/Tools/Core/ToolTypes.h
```

---

### 2.9 Rendering: 旧 `Graphics` の再分類

旧 `Engine/Core/Graphics` は `Engine/Core/Rendering` にRenameし、内部を以下へ分割する。

```txt
Engine/Core/Graphics/GraphicsCore.*              -> Engine/Core/Rendering/Core/RenderingCore.*
Engine/Core/Graphics/GraphicsPlatform.*          -> Engine/Core/Rendering/Core/RenderingPlatform.*
Engine/Core/Graphics/GraphicsFeatureController.* -> Engine/Core/Rendering/Core/RenderingFeatureController.*
Engine/Core/Graphics/GraphicsFeatureTypes.*      -> Engine/Core/Rendering/Core/RenderingFeatureTypes.*

Engine/Core/Graphics/DxLib/      -> Engine/Core/Rendering/RHI/DirectX12/Common/
Engine/Core/Graphics/DxObject/   -> Engine/Core/Rendering/RHI/DirectX12/Core/
Engine/Core/Graphics/Descriptors/ -> Engine/Core/Rendering/RHI/DirectX12/Descriptors/
Engine/Core/Graphics/GPUBuffer/  -> Engine/Core/Rendering/RHI/DirectX12/Buffers/

Engine/Core/Graphics/Asset/      -> Engine/Core/Rendering/Assets/
Engine/Core/Graphics/Material/   -> Engine/Core/Rendering/Materials/
Engine/Core/Graphics/Texture/    -> Engine/Core/Rendering/Textures/
Engine/Core/Graphics/Mesh/       -> Engine/Core/Rendering/Meshes/
Engine/Core/Graphics/Pipeline/   -> Engine/Core/Rendering/Pipelines/
Engine/Core/Graphics/Raytracing/ -> Engine/Core/Rendering/Raytracing/
Engine/Core/Graphics/Line/       -> Engine/Core/Rendering/DebugDraw/Lines/
Engine/Core/Graphics/Render/     -> Engine/Core/Rendering/Renderer/
```

#### Rendering Core Rename

```txt
GraphicsCore.*              -> RenderingCore.*
GraphicsPlatform.*          -> RenderingPlatform.*
GraphicsFeatureController.* -> RenderingFeatureController.*
GraphicsFeatureTypes.*      -> RenderingFeatureTypes.*
```

クラス名は今回は変更しなくてよい。ファイル名とincludeのみ変更する。

#### DirectX12 RHI Rename

```txt
DxCommand.*        -> D3D12CommandContext.*
DxUploadCommand.*  -> D3D12UploadContext.*
DxDevice.*         -> D3D12Device.*
DxSwapChain.*      -> D3D12SwapChain.*
DxShaderCompiler.* -> D3D12ShaderCompiler.*
DxUtils.*          -> D3D12Utils.*
DxStructures.h     -> D3D12Types.h
ComPtr.h           -> ComPtr.h  # 変更なし
BaseDescriptor.*   -> D3D12Descriptor.*
RTVDescriptor.*    -> D3D12RenderTargetView.*
DSVDescriptor.*    -> D3D12DepthStencilView.*
SRVDescriptor.*    -> D3D12ShaderResourceView.*
DxConstBuffer.h    -> D3D12ConstantBuffer.h
DxStructuredBuffer.h -> D3D12StructuredBuffer.h
DxReadbackBuffer.h -> D3D12ReadbackBuffer.h
StructuredRWBuffer.h -> D3D12RWStructuredBuffer.h
IndexBuffer.*      -> IndexBuffer.*
VertexBuffer.h     -> VertexBuffer.h
RenderBufferRegistry.* -> RenderBufferRegistry.*
```

#### Renderer 内部整理

```txt
Engine/Core/Rendering/Renderer/Backend/Interface/ -> Engine/Core/Rendering/Renderer/Backends/Core/
Engine/Core/Rendering/Renderer/Backend/Registry/  -> Engine/Core/Rendering/Renderer/Backends/Registry/
Engine/Core/Rendering/Renderer/Backend/Common/    -> Engine/Core/Rendering/Renderer/Backends/Common/
Engine/Core/Rendering/Renderer/Backend/Builtin/   -> Engine/Core/Rendering/Renderer/Backends/Builtin/
Engine/Core/Rendering/Renderer/Light/             -> Engine/Core/Rendering/Renderer/Lighting/
Engine/Core/Rendering/Renderer/Pass/              -> Engine/Core/Rendering/Renderer/Passes/
Engine/Core/Rendering/Renderer/Queue/             -> Engine/Core/Rendering/Renderer/Queues/
Engine/Core/Rendering/Renderer/Texture/           -> Engine/Core/Rendering/Renderer/RenderTargets/
Engine/Core/Rendering/Renderer/View/              -> Engine/Core/Rendering/Renderer/Views/
RenderPipelineRunner.*                            -> Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.*
```

---

## 3. Codexに実行させる作業手順

### Step 1: 現状を確認

```powershell
cd Engine
Get-ChildItem -Recurse Core | Select-Object FullName
```

### Step 2: Git作業ブランチ作成

```bash
git checkout -b refactor/core-folder-layout
```

### Step 3: ディレクトリ作成・移動

上記マッピングに従って `git mv` で移動する。通常の `mv` ではなく、履歴追跡のため可能な限り `git mv` を使う。

PowerShellで実行する場合の補助関数:

```powershell
function Move-FileSafe($from, $to) {
  if (Test-Path $from) {
    $dir = Split-Path $to -Parent
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    git mv $from $to
  }
}

function Move-DirSafe($from, $to) {
  if (Test-Path $from) {
    New-Item -ItemType Directory -Force -Path $to | Out-Null
    Get-ChildItem $from -Force | ForEach-Object {
      git mv $_.FullName (Join-Path $to $_.Name)
    }
  }
}
```

### Step 4: include更新

全C++ファイルを対象に、旧パスを新パスへ置換する。

対象拡張子:

```txt
*.h, *.hpp, *.cpp, *.inl, *.ipp
```

置換対象:

- `#include "Graphics/..."` -> `#include "Core/Rendering/..."` または相対関係に応じた新パス
- `#include "Asset/..."` -> `#include "Core/Assets/..."`
- `#include "ECS/..."` -> `#include "Core/World/ECS/..."` または `Core/World/Components/...`
- `#include "Scene/..."` -> `#include "Core/World/Scene/..."`
- `#include "Prefab/..."` -> `#include "Core/World/Prefab/..."`
- `#include "Collision/..."` -> `#include "Core/Physics/Collision/..."`
- `#include "Logger/..."` -> `#include "Core/Foundation/Diagnostics/..."`
- `#include "Math/..."` -> `#include "Core/Foundation/Math/..."`
- `#include "UUID/..."` -> `#include "Core/Foundation/Identity/..."`
- `#include "Utility/Json/..."` -> `#include "Core/Foundation/Serialization/Json/..."`
- `#include "Utility/..."` -> `#include "Core/Foundation/Utility/..."`
- `#include "Window/..."` -> `#include "Core/Platform/Windows/..."`
- `#include "Input/..."` -> `#include "Core/Platform/Input/..."`
- `#include "Context/..."` -> `#include "Core/Runtime/Context/..."`
- `#include "Runtime/..."` -> `#include "Core/Runtime/..."`

自己ヘッダの例:

```cpp
// D3D12Device.cpp
#include "D3D12Device.h"
```

クロスモジュール参照の例:

```cpp
#include "Core/Foundation/Math/Vector3.h"
#include "Core/Rendering/RHI/DirectX12/Core/D3D12Device.h"
#include "Core/World/Components/Rendering/MeshRendererComponent.h"
```

### Step 5: `.vcxproj` / `.filters` 更新

`Engine/NEMEngine.vcxproj` と `Engine/NEMEngine.vcxproj.filters` 内の全パスを新構成へ更新する。

注意:

- `.vcxproj` の `Include="Core\..."` を新パスにする。
- `.filters` の `<Filter>Core\...</Filter>` も新パスにする。
- 存在しない旧パスが残っていないこと。
- 新規ディレクトリの Filter を必要に応じて追加する。

確認コマンド:

```powershell
Select-String -Path Engine/NEMEngine.vcxproj,Engine/NEMEngine.vcxproj.filters -Pattern "Core\\Graphics|Core\\Asset|Core\\Collision|Core\\Logger|Core\\UUID|Core\\Utility|Core\\Window|Core\\Context"
```

この検索で、意図的に残すもの以外はヒットしない状態にする。

### Step 6: ファイル名Renameに伴う include名更新

少なくとも以下のinclude名は全置換する。

```txt
Framework.h -> EngineFramework.h
Logger.h -> Log.h
UUID.h -> Uuid.h
TypeHash.h -> TypeId.h
JsonAdapter.h -> JsonSerializer.h
MyGUI.h -> ImGuiHelpers.h
Input.h -> InputSystem.h
InputStructures.h -> InputTypes.h
IInputDevice.h -> InputDevice.h
Audio.h -> AudioSystem.h
GraphicsCore.h -> RenderingCore.h
GraphicsPlatform.h -> RenderingPlatform.h
GraphicsFeatureController.h -> RenderingFeatureController.h
GraphicsFeatureTypes.h -> RenderingFeatureTypes.h
DxCommand.h -> D3D12CommandContext.h
DxUploadCommand.h -> D3D12UploadContext.h
DxDevice.h -> D3D12Device.h
DxSwapChain.h -> D3D12SwapChain.h
DxShaderCompiler.h -> D3D12ShaderCompiler.h
DxUtils.h -> D3D12Utils.h
DxStructures.h -> D3D12Types.h
BaseDescriptor.h -> D3D12Descriptor.h
RTVDescriptor.h -> D3D12RenderTargetView.h
DSVDescriptor.h -> D3D12DepthStencilView.h
SRVDescriptor.h -> D3D12ShaderResourceView.h
DxConstBuffer.h -> D3D12ConstantBuffer.h
DxStructuredBuffer.h -> D3D12StructuredBuffer.h
DxReadbackBuffer.h -> D3D12ReadbackBuffer.h
StructuredRWBuffer.h -> D3D12RWStructuredBuffer.h
TransformUpdateSystem.h -> TransformSystem.h
SkinnedAnimationUpdateSystem.h -> SkinnedAnimationSystem.h
```

`.cpp` 側も同様。

### Step 7: 旧パス文字列の残存確認

```powershell
$patterns = @(
  "Core/Graphics", "Core\\Graphics",
  "Core/Asset", "Core\\Asset",
  "Core/Collision", "Core\\Collision",
  "Core/Logger", "Core\\Logger",
  "Core/UUID", "Core\\UUID",
  "Core/Utility", "Core\\Utility",
  "Core/Window", "Core\\Window",
  "Core/Context", "Core\\Context",
  "#include \"Graphics/", "#include \"Asset/", "#include \"ECS/", "#include \"Collision/"
)
foreach ($p in $patterns) {
  Select-String -Path Engine/**/*.h,Engine/**/*.cpp,Engine/NEMEngine.vcxproj,Engine/NEMEngine.vcxproj.filters -Pattern $p -SimpleMatch
}
```

### Step 8: ビルド確認

Visual StudioまたはMSBuildでビルドする。

```powershell
msbuild Engine/NEMEngine.vcxproj /p:Configuration=Debug /p:Platform=x64
```

もしMSBuildが使えない環境なら、少なくとも以下を確認する。

```powershell
# 旧パスの参照が残っていない
Select-String -Path Engine/**/*.h,Engine/**/*.cpp -Pattern "Core/Graphics|Core/Asset|Core/Collision|Core/Logger|Core/UUID|Core/Utility|Core/Window|Core/Context"

# vcxprojに存在しないファイルパスがないか確認
# Codex側で簡易スクリプトを書いてチェックすること
```

---

## 4. 受け入れ条件

1. `Engine/Core/Graphics` が存在せず、`Engine/Core/Rendering` に移行済み。
2. `Engine/Core/Asset` が存在せず、`Engine/Core/Assets` に移行済み。
3. `Engine/Core/Collision` が存在せず、`Engine/Core/Physics/Collision` に移行済み。
4. `Engine/Core/Logger`, `Engine/Core/UUID`, `Engine/Core/Utility`, `Engine/Core/Window`, `Engine/Core/Context` が残っていない。
5. `Engine/Core/Foundation`, `Runtime`, `Platform`, `Assets`, `World`, `Rendering`, `Scripting`, `Animation`, `Physics`, `Audio`, `Tools` の大分類が存在する。
6. `Engine/NEMEngine.vcxproj` と `.filters` が新パスを参照している。
7. `#include` の旧パス参照が残っていない。
8. Debug x64 ビルドが通る。

---

## 5. 最終的な理想ツリー概要

```txt
Engine/Core/
  Foundation/
    Build/
    Diagnostics/
      Assert.*
      Log.*
    Identity/
      Uuid.*
      TypeId.*
    Math/
    Serialization/
      Json/
        JsonSerializer.*
    Time/
    Utility/
      Algorithm/
      Command/
      Enum/
      Random/

  Runtime/
    Application/
      EngineApplication.*
    Context/
      EngineContext.*
    Framework/
      EngineFramework.*
    Paths/
      RuntimePaths.*

  Platform/
    Windows/
      Win32Window.*
    Input/
      Devices/
      InputSystem.*
      InputTypes.*
      InputMapper.h

  Assets/
    AssetTypes.*
    Async/
    Database/

  World/
    ECS/
      Config/
      Entity/
      World/
      Components/
      Systems/
    Components/
      Animation/
      Audio/
      Camera/
      Lighting/
      Physics/
      Prefab/
      Rendering/
      Scene/
      Scripting/
      Transform/
    Systems/
      Animation/
      Audio/
      Behavior/
      Camera/
      Hierarchy/
      Physics/
      Rendering/
      Transform/
    Behavior/
    Scene/
      Authoring/
      Runtime/
      Serialization/
    Prefab/
      Runtime/
      Serialization/

  Rendering/
    Core/
    Assets/
    Materials/
    Textures/
    Meshes/
    Pipelines/
    Raytracing/
    DebugDraw/
      Lines/
    RHI/
      DirectX12/
        Common/
        Core/
        Descriptors/
        Buffers/
    Renderer/
      Backends/
      Lighting/
      Passes/
      Pipeline/
      Queues/
      RenderTargets/
      Views/

  Scripting/
    Managed/

  Animation/
    Clips/
    Curves/
    Evaluation/
    Properties/

  Physics/
    Collision/

  Audio/
    AudioSystem.*

  Tools/
    Core/
    Registry/
    ImGui/
```

---

## 6. 実装後に出やすいエラーと対処

### include file not found

原因: ファイル移動後の include 置換漏れ。

対処: エラーの出たヘッダ名を `Get-ChildItem -Recurse Engine/Core -Filter <name>` で探し、新パスに直す。

### `.vcxproj` が旧ファイルを参照している

原因: `git mv` だけでは Visual Studio project file が自動更新されない。

対処: `NEMEngine.vcxproj` / `.filters` の `Core\旧パス` を新パスに置換する。

### 同名ヘッダの曖昧さ

例: `Input.h`, `Audio.h`, `Logger.h` など一般名。

対処: 今回のRename対象に従い、`InputSystem.h`, `AudioSystem.h`, `Log.h` へ変更する。

### クラス名とファイル名が違う

今回はファイル名整理を優先し、クラス名変更は必須ではない。ビルドが安定した後、別PRでクラス名Renameを行う。

---

## 7. Codexへの最終プロンプト例

以下をCodex CLIに渡して実行してください。

```txt
NEMEngine_02 の Engine/Core フォルダを、添付の「NEMEngine_Core_Reorg_Codex.md」に従って再編してください。

必須条件:
- 動作変更はしない
- namespace は変更しない
- git mv を優先する
- Engine/NEMEngine.vcxproj と Engine/NEMEngine.vcxproj.filters を新パスへ更新する
- includeを全て新パスへ修正する
- 旧 Core/Graphics, Core/Asset, Core/Collision, Core/Logger, Core/UUID, Core/Utility, Core/Window, Core/Context が残らないようにする
- 可能なら Debug x64 でビルド確認する
- ビルドできない環境なら、旧パス残存チェックと vcxproj の存在チェックを実行し、結果を報告する

完了時に以下を報告してください:
1. 移動・Renameした主なディレクトリ
2. 更新したincludeの方針
3. 更新したVisual Studio project/filter
4. ビルド結果または代替検証結果
5. 残課題
```
