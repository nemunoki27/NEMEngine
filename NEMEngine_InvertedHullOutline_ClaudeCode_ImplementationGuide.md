# NEMEngine 背面法アウトライン 一括実装指示書

## 0. Claude Code CLI への最重要指示

この文書を最後まで読んでから、次のローカルリポジトリを直接編集してください。

```text
C:\Users\k023g\school\NEMProjects\NEMEngine
```

この作業は **段階ごとに停止して確認を求める作業ではありません**。文書内の「実装順序」は依存関係を整理するための順番であり、途中終了地点ではありません。最終的に、背面法アウトラインの基本機能と表現拡張を **一回の実装作業でまとめて完成**させてください。

必須条件:

- `InvertedHullOutlineComponent` を追加したエンティティだけにアウトラインを適用する。
- 通常の Opaque 描画を維持し、追加描画としてアウトラインを描く。
- **Vertex Shader 経路と Mesh Shader 経路の両方を必ず実装する。**
- Mesh Shader 経路では Amplification Shader も追加する。
- Vertex Shader 版だけを実装して終了しない。
- Mesh Shader 版を TODO、ダミー、フォールバック固定、常時 Vertex 強制で済ませない。
- スキニング済み頂点を VS / MS の両方で利用できる既存設計を壊さない。
- 次の表現拡張をすべて実装する。
  - 法線方向膨張
  - Position Scaling 膨張
  - モデル空間幅
  - スクリーン空間ピクセル幅
  - Camera Z Offset
  - Baked Normal Texture
  - 部位別アウトライン幅を制御する Outline Sampler Texture
  - 任意のステンシル抑制 (`useStencil`)
- Inspector から全設定を編集できるようにする。
- JSON シリアライズ / デシリアライズ、静的バッチキャッシュ、テクスチャ解決、プロジェクトファイル登録まで完了させる。
- 新規コードに未解決の TODO や仮実装を残さない。
- 最後にビルドを実行し、可能な範囲で実行時検証を行う。

実装中に既存コードの型名や API がこの指示書と少し異なる場合は、リポジトリの現物を優先してください。ただし、要件を削らず、同じ責務を満たす形へ適応してください。

---

## 1. 実装目標

背面法アウトラインは、通常描画済みのメッシュを法線方向またはピボット放射方向へ膨張させ、前面をカリングして背面だけを単色描画する方式で実装する。

```text
通常メッシュ描画
    Back Face Cull
    通常 PBR / Transparent 描画

アウトライン追加描画
    頂点を膨張
    Front Face Cull
    単色 Unlit 描画
    SceneMain Depth で Depth Test
    Depth Write は無効
```

`D3D12_CULL_MODE_FRONT` は前面三角形を描画しない設定であり、膨張済み Hull の背面だけを残すために利用する。アウトラインの深度テストは有効、深度書き込みは `D3D12_DEPTH_WRITE_MASK_ZERO` とする。

実装リファレンス:

- Microsoft `D3D12_CULL_MODE`: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_cull_mode
- Microsoft `D3D12_DEPTH_WRITE_MASK`: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_depth_write_mask
- Microsoft DirectX Graphics Samples / Meshlet Culling: https://github.com/microsoft/DirectX-Graphics-Samples
- Unity Universal Rendering Examples / Toon Outline: https://github.com/Unity-Technologies/UniversalRenderingExamples/wiki/Toon-Outline
- UnityChan Toon Shader 2.0 Manual: https://github.com/unity3d-jp/UnityChanToonShaderVer2_Project/blob/release/legacy/2.0/Manual/UTS2_Manual_ja.md

---

## 2. 現在のエンジン構成に対する実装方針

### 2.1 RenderPath へ専用追加パスを挿入する

現状の固定 RenderPath は `Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.cpp` で次の順序になっている。

```text
ClearRenderTargetsPass
DepthPrepass
LightCullingPass
OpaqueRenderPass
RaytracingReflectionPass
TransparentRenderPass
PostProcessMaskedUiPass
PostProcessStackPass
BlitToViewPass
ScreenUiPass
DebugOverlayPass
EditorOverlayPass
```

新規 `InvertedHullOutlinePass` を **`RaytracingReflectionPass` の後、`TransparentRenderPass` の前**へ追加する。

```text
ClearRenderTargetsPass
DepthPrepass
LightCullingPass
OpaqueRenderPass
RaytracingReflectionPass
InvertedHullOutlinePass       <- 新規
TransparentRenderPass
PostProcessMaskedUiPass
PostProcessStackPass
BlitToViewPass
ScreenUiPass
DebugOverlayPass
EditorOverlayPass
```

理由:

- `OpaqueRenderPass` は `SceneMain` へ通常色、法線、位置、深度を書き込む。
- `RaytracingReflectionPass` は反射合成結果を `SceneFinal` へ書き込む。
- アウトラインは物理表面ではなく演出色なので、反射計算の入力へ含めず、反射合成後の `SceneFinal` へ描く。
- 透明物の前へ描くことで、透明描画の既存順序を維持する。

`passes_.reserve(12)` は `passes_.reserve(13)` へ変更する。

### 2.2 `RenderPhase::Outline` は追加しない

`RenderPhase` へ Outline を追加して、エンティティの通常キューを Outline に置き換えてはいけない。背面法アウトラインは主描画ではなく追加描画である。

`InvertedHullOutlinePass` は `RenderPhase::Opaque` の既存 `RenderItem` から対象を抽出し、同じ `RenderItem` を再利用する。

対象条件:

```text
RenderPhase::Opaque のアイテム
backendID == RenderBackendID::Mesh
item->world != nullptr
カメラの visibilityLayerMask を通過
InvertedHullOutlineComponent が存在
component.enabled == true
```

Transparent 用アウトラインは今回の対象外とする。まず Opaque エンティティへ正しく適用する。

### 2.3 `SceneFinal` の色と `SceneMain` の深度を組み合わせる

`Core/Rendering/Renderer/RenderPath/RenderPathResources.cpp` では:

```text
SceneMain
    SceneColorMain
    SceneNormalMain
    ScenePositionMain
    SceneDepth: DXGI_FORMAT_D24_UNORM_S8_UINT

SceneFinal
    SceneColorFinal
    深度なし
```

アウトライン Hull 描画は次を同時にバインドする。

```text
RTV: SceneFinal の色
DSV: SceneMain の SceneDepth
```

既存 `MultiRenderTarget` は自身の RTV と自身の DSV をまとめてバインドする設計なので、外部 DSV を渡せる共通描画バインド拡張を追加する。

---

## 3. 新規コンポーネント

### 3.1 追加ファイル

```text
Core/World/Components/Rendering/InvertedHullOutlineComponent.h
Core/World/Components/Rendering/InvertedHullOutlineComponent.cpp
```

### 3.2 コンポーネント仕様

次の責務を持つコンポーネントを追加する。命名は既存の規約へ合わせて微調整してよいが、JSON 登録名は `"InvertedHullOutline"` とする。

```cpp
#pragma once

#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

namespace Engine {

    enum class OutlineExpansionMode : uint8_t {
        NormalDirection,
        PositionScaling,
    };

    enum class OutlineWidthMode : uint8_t {
        ModelUnits,
        ScreenPixels,
    };

    struct InvertedHullOutlineComponent {
        bool enabled = true;

        // ModelUnits ではモデル空間距離、ScreenPixels では画面ピクセル数。
        float width = 0.01f;
        Color4 color = Color4::Black();

        OutlineExpansionMode expansionMode = OutlineExpansionMode::NormalDirection;
        OutlineWidthMode widthMode = OutlineWidthMode::ModelUnits;

        // 正値でカメラから奥へ押し込む。
        float cameraZOffset = 0.0f;

        // RGB に object/local-space normal を [0, 1] エンコードした Linear texture。
        bool useBakedNormal = false;
        AssetID bakedNormalTexture{};

        // R チャンネル。0 で膨張なし、1 で width をそのまま適用。
        // 部位別アウトライン幅と線の抑制に使用する Linear texture。
        bool useOutlineSampler = false;
        AssetID outlineSamplerTexture{};

        // true の場合、同一フレームの outlined silhouette を stencil へ書き込み、
        // Hull 描画時に NOT_EQUAL で内部や重なりを抑制する。
        bool useStencil = false;
    };

    void from_json(const nlohmann::json& in, InvertedHullOutlineComponent& component);
    void to_json(nlohmann::json& out, const InvertedHullOutlineComponent& component);

    ENGINE_REGISTER_COMPONENT(InvertedHullOutlineComponent, "InvertedHullOutline");
}
```

### 3.3 JSON 変換

既存 `MeshRendererComponent.cpp` のスタイルへ合わせる。

```cpp
component.enabled = in.value("enabled", component.enabled);
component.width = (std::max)(0.0f, in.value("width", component.width));
component.color = Color4::FromJson(in.value("color", nlohmann::json{}));
component.expansionMode = EnumAdapter<OutlineExpansionMode>::FromString(
    in.value("expansionMode", "NormalDirection"))
    .value_or(OutlineExpansionMode::NormalDirection);
component.widthMode = EnumAdapter<OutlineWidthMode>::FromString(
    in.value("widthMode", "ModelUnits"))
    .value_or(OutlineWidthMode::ModelUnits);
component.cameraZOffset = in.value("cameraZOffset", component.cameraZOffset);
component.useBakedNormal = in.value("useBakedNormal", component.useBakedNormal);
component.bakedNormalTexture = ParseAssetID(in, "bakedNormalTexture");
component.useOutlineSampler = in.value("useOutlineSampler", component.useOutlineSampler);
component.outlineSamplerTexture = ParseAssetID(in, "outlineSamplerTexture");
component.useStencil = in.value("useStencil", component.useStencil);
```

`to_json` では `ToString(AssetID)` と `Color4::ToJson()` を使う。

無効テクスチャの場合は Texture AssetID を空文字列として保存してよい。

---

## 4. 新規 RenderPass

### 4.1 追加ファイル

```text
Core/Rendering/Renderer/RenderPath/Passes/InvertedHullOutlinePass.h
Core/Rendering/Renderer/RenderPath/Passes/InvertedHullOutlinePass.cpp
```

### 4.2 パスの責務

`InvertedHullOutlinePass` は Opaque バケットから対象を集め、`useStencil` に応じて二群へ分ける。

```cpp
struct OutlineItemGroups {
    std::vector<const RenderItem*> regularItems;
    std::vector<const RenderItem*> stencilItems;
};
```

実行順序:

```text
1. regularItems があれば:
   SceneFinal RTV + SceneMain DSV で "Outline" を描画

2. stencilItems があれば:
   SceneMain の stencil のみ 0 で clear
   OMSetStencilRef(1)
   SceneMain DSV のみで "OutlineStencilWrite" を描画
   SceneFinal RTV + SceneMain DSV で "OutlineStencilTest" を描画
   OMSetStencilRef(0) へ戻す
```

ステンシル予約値:

```cpp
static constexpr UINT kOutlineStencilReference = 1u;
```

この値は全 outlined entity 共通とする。エンティティごとに異なる stencil reference を持たせると、現状のインスタンスバッチを分割する必要が増えるため、今回の用途では不要。

### 4.3 CollectItems 実装

`DepthPrepass::CollectItems()` を参考にする。

```cpp
OutlineItemGroups InvertedHullOutlinePass::CollectItems(
    const SceneExecutionContext& context,
    const RenderPassPhaseBuckets& passBuckets) const {

    OutlineItemGroups result{};
    const RenderPassItemList* list = passBuckets.Find(RenderPhase::Opaque);
    if (!list || list->IsEmpty()) {
        return result;
    }

    const ResolvedCameraView* camera = context.view
        ? context.view->FindCamera(RenderCameraDomain::Perspective)
        : nullptr;
    if (!camera) {
        return result;
    }

    result.regularItems.reserve(list->items.size());
    result.stencilItems.reserve(list->items.size());

    for (const RenderItem* item : list->items) {
        if (!item || item->backendID != RenderBackendID::Mesh || !item->world) {
            continue;
        }
        if ((item->visibilityLayerMask & camera->cullingMask) == 0) {
            continue;
        }
        const auto* outline = item->world->TryGetComponent<InvertedHullOutlineComponent>(item->entity);
        if (!outline || !outline->enabled || outline->width <= 0.0f) {
            continue;
        }
        (outline->useStencil ? result.stencilItems : result.regularItems).emplace_back(item);
    }
    return result;
}
```

### 4.4 Stencil の clear

現在 `ClearRenderTargetsPass` は `clearStencil = false` のため、outline stencil を使う場合は `InvertedHullOutlinePass` 内で SceneMain の stencil のみ clear する。

```cpp
auto* sceneMain = context.resources->GetSceneMain();
auto* depth = sceneMain ? sceneMain->GetDepthTexture() : nullptr;
if (depth) {
    depth->Transition(*dxCommand, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    MultiRenderTargetClearDesc clear{};
    clear.clearColor = false;
    clear.clearDepth = false;
    clear.clearStencil = true;
    clear.clearStencilValue = 0;
    sceneMain->Clear(*dxCommand, clear);
}
```

深度値を消してはいけない。

---

## 5. 外部 DSV を利用できる描画ヘルパー拡張

### 5.1 変更対象

```text
Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h
Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.cpp
Core/Rendering/Renderer/Passes/RenderItemBatchDispatcher.h
Core/Rendering/Renderer/Passes/RenderItemBatchDispatcher.cpp
```

### 5.2 Surface Binding を追加

`RenderPassExecutionHelper.h` に次のような構造体を追加する。

```cpp
struct RenderPassSurfaceBinding {
    MultiRenderTarget* colorSurface = nullptr;
    DepthTexture2D* depthOverride = nullptr;
};
```

既存の `Execute(..., MultiRenderTarget* target, ...)` は残し、内部で `RenderPassSurfaceBinding{ target, nullptr }` に変換する。既存呼び出しを壊さない。

外部 DSV 対応版を追加する。

```cpp
void Execute(
    GraphicsCore& graphicsCore,
    SceneExecutionContext& context,
    const std::vector<const RenderItem*>& items,
    const RenderPipelineDeps& deps,
    const RenderPassSurfaceBinding& surface,
    const char* drawPassName = "Draw",
    bool forceVertexMeshVariant = false,
    bool depthOnly = false);
```

### 5.3 非 depth-only 時のバインド

`colorSurface` の RTV と `depthOverride` を明示的に組み合わせる。

```cpp
MultiRenderTarget* target = surface.colorSurface;
DepthTexture2D* depth = surface.depthOverride
    ? surface.depthOverride
    : (target ? target->GetDepthTexture() : nullptr);

std::vector<RenderTarget> renderTargets{};
renderTargets.reserve(target->GetColorCount());
for (uint32_t i = 0; i < target->GetColorCount(); ++i) {
    auto* color = target->GetColorTexture(i);
    if (!color) {
        continue;
    }
    color->Transition(*dxCommand, D3D12_RESOURCE_STATE_RENDER_TARGET);
    renderTargets.emplace_back(color->GetRenderTarget());
}
if (depth) {
    depth->Transition(*dxCommand, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}
dxCommand->BindRenderTargets(
    renderTargets,
    depth ? std::optional<D3D12_CPU_DESCRIPTOR_HANDLE>(depth->GetDSVCPUHandle()) : std::nullopt);
```

アウトライン用呼び出し:

```cpp
RenderPassSurfaceBinding binding{};
binding.colorSurface = context.resources->GetSceneFinal();
binding.depthOverride = context.resources->GetSceneMain()->GetDepthTexture();

RenderPassExecutionHelper::Execute(
    graphicsCore, context, items, deps_, binding, "Outline", false, false);
```

### 5.4 Dispatcher に depth override を伝える

`RenderItemBatchDispatcher::Dispatch` に `const DepthTexture2D* depthOverride` を追加する。

```cpp
void Dispatch(...,
    const MultiRenderTarget* surface,
    const DepthTexture2D* depthOverride,
    const std::string_view& passName,
    bool depthOnly) const;
```

フォーマット解決:

```cpp
const DepthTexture2D* boundDepth = depthOverride
    ? depthOverride
    : (surface ? surface->GetDepthTexture() : nullptr);

if (depthOnly) {
    drawContext.dsvFormat = boundDepth ? boundDepth->GetDSVFormat() : DXGI_FORMAT_UNKNOWN;
} else {
    FillColorFormats(surface, drawContext.rtvFormats, drawContext.numRTVFormats);
    drawContext.dsvFormat = boundDepth ? boundDepth->GetDSVFormat() : DXGI_FORMAT_UNKNOWN;
}
```

### 5.5 既存バグを同時修正する

現状の `RenderPassExecutionHelper.cpp` は `DispatchInternal(..., depthOnly)` を受け取っているが、Dispatcher 呼び出し時に末尾引数を常に `false` としている。

誤り:

```cpp
deps.dispatcher->Dispatch(..., target, drawPassName, false);
```

修正:

```cpp
deps.dispatcher->Dispatch(..., target, depthOverride, drawPassName, depthOnly);
```

この修正を忘れると `OutlineStencilWrite` および既存 ZPrepass のフォーマット解決が不正確になる。

---

## 6. アウトライン用デフォルトマテリアル

### 6.1 MaterialResolver のスロット追加

変更対象:

```text
Core/Rendering/Materials/MaterialResolver.h
Core/Rendering/Materials/MaterialResolver.cpp
```

`DefaultMaterialSlot` に `MeshOutline` を追加する。

```cpp
enum class DefaultMaterialSlot : uint8_t {
    Sprite,
    Text,
    Mesh,
    MeshOutline,
    FullscreenCopy,
};
```

`kDefaultMaterialCount` は最後の列挙値に追従する既存計算を維持する。

`EnsureDefaults()`:

```cpp
tryImportIfExists(DefaultMaterialSlot::MeshOutline, AssetType::Material);
```

`GetDefaultAssetPath()`:

```cpp
case DefaultMaterialSlot::MeshOutline:
    return "Engine/Assets/Materials/Builtin/Mesh/defaultMeshOutline.material.json";
```

### 6.2 MeshRenderBackend のパス解決

変更対象:

```text
Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.cpp
```

`ResolveMeshPass()` の先頭側で、次の 3 パスを元マテリアルと切り離して `MeshOutline` デフォルトマテリアルから解決する。

```cpp
if (context.passName == "Outline" ||
    context.passName == "OutlineStencilWrite" ||
    context.passName == "OutlineStencilTest") {

    return BackendDrawCommon::ResolveMaterialPass(
        context,
        AssetID{},
        DefaultMaterialSlot::MeshOutline,
        { context.passName },
        outResolved);
}
```

コンポーネントを追加するだけで任意の既存マテリアルへアウトラインを適用できるようにする。各ユーザーマテリアルへ Outline Pass を追加させてはいけない。

### 6.3 新規マテリアル JSON

追加:

```text
Assets/Materials/Builtin/Mesh/defaultMeshOutline.material.json
```

内容:

```json
{
  "name": "DefaultMeshOutline",
  "domain": "Surface",
  "passes": [
    {
      "passName": "Outline",
      "pipeline": "Engine/Assets/Pipelines/Builtin/Mesh/defaultMeshOutline.pipeline.json",
      "preferredVariant": "GraphicsMesh"
    },
    {
      "passName": "OutlineStencilWrite",
      "pipeline": "Engine/Assets/Pipelines/Builtin/Mesh/defaultMeshOutlineStencilWrite.pipeline.json",
      "preferredVariant": "GraphicsMesh"
    },
    {
      "passName": "OutlineStencilTest",
      "pipeline": "Engine/Assets/Pipelines/Builtin/Mesh/defaultMeshOutlineStencilTest.pipeline.json",
      "preferredVariant": "GraphicsMesh"
    }
  ]
}
```

---

## 7. Pipeline Asset のステンシル JSON 対応

### 7.1 変更対象

```text
Core/Rendering/Assets/RenderPipelineAsset.cpp
```

現状 `ParseDepthStencil()` は `depthEnable`、`depthWriteMask`、`depthFunc`、`stencilEnable` までしか読まない。次を追加する。

```text
stencilReadMask
stencilWriteMask
frontFace.stencilFailOp
frontFace.stencilDepthFailOp
frontFace.stencilPassOp
frontFace.stencilFunc
backFace.stencilFailOp
backFace.stencilDepthFailOp
backFace.stencilPassOp
backFace.stencilFunc
```

ヘルパー例:

```cpp
D3D12_DEPTH_STENCILOP_DESC ParseStencilOpDesc(
    const nlohmann::json& data,
    const D3D12_DEPTH_STENCILOP_DESC& fallback) {

    D3D12_DEPTH_STENCILOP_DESC desc = fallback;
    if (!data.is_object()) {
        return desc;
    }

    desc.StencilFailOp = EnumAdapter<D3D12_STENCIL_OP>::FromString(
        data.value("stencilFailOp", std::string(EnumAdapter<D3D12_STENCIL_OP>::ToString(desc.StencilFailOp))))
        .value_or(desc.StencilFailOp);
    desc.StencilDepthFailOp = EnumAdapter<D3D12_STENCIL_OP>::FromString(
        data.value("stencilDepthFailOp", std::string(EnumAdapter<D3D12_STENCIL_OP>::ToString(desc.StencilDepthFailOp))))
        .value_or(desc.StencilDepthFailOp);
    desc.StencilPassOp = EnumAdapter<D3D12_STENCIL_OP>::FromString(
        data.value("stencilPassOp", std::string(EnumAdapter<D3D12_STENCIL_OP>::ToString(desc.StencilPassOp))))
        .value_or(desc.StencilPassOp);
    desc.StencilFunc = EnumAdapter<D3D12_COMPARISON_FUNC>::FromString(
        data.value("stencilFunc", std::string(EnumAdapter<D3D12_COMPARISON_FUNC>::ToString(desc.StencilFunc))))
        .value_or(desc.StencilFunc);
    return desc;
}
```

`ParseDepthStencil()` 内:

```cpp
desc.StencilReadMask = data.value("stencilReadMask", desc.StencilReadMask);
desc.StencilWriteMask = data.value("stencilWriteMask", desc.StencilWriteMask);
desc.FrontFace = ParseStencilOpDesc(data.value("frontFace", nlohmann::json::object()), desc.FrontFace);
desc.BackFace = ParseStencilOpDesc(data.value("backFace", nlohmann::json::object()), desc.BackFace);
```

`ToJson()` も Rasterizer / DepthStencil / StaticSampler を現在十分に書き戻していない。今回追加する値を落とさないよう、少なくとも stencil 対応を含む `depthStencil` と `rasterizer` を対称に書き戻す。既存 pipeline JSON のロードを壊さない。

---

## 8. 新規 Pipeline JSON

### 8.1 Hull 描画: stencil なし

追加:

```text
Assets/Pipelines/Builtin/Mesh/defaultMeshOutline.pipeline.json
```

両 variant を必ず定義する。

```json
{
  "name": "DefaultMeshOutlinePipeline",
  "variants": [
    {
      "kind": "GraphicsMesh",
      "pipelineType": "Mesh",
      "shader": "Engine/Assets/Shaders/Builtin/Mesh/defaultMeshOutline.shader.json",
      "numRenderTargets": 1,
      "dynamicRenderTargetFormats": true,
      "dsvFormat": "DXGI_FORMAT_UNKNOWN",
      "topologyType": "D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE",
      "requiresMeshShader": true,
      "rasterizer": {
        "fillMode": "D3D12_FILL_MODE_SOLID",
        "cullMode": "D3D12_CULL_MODE_FRONT",
        "frontCounterClockwise": false,
        "depthClipEnable": true
      },
      "depthStencil": {
        "depthEnable": true,
        "depthWriteMask": "D3D12_DEPTH_WRITE_MASK_ZERO",
        "depthFunc": "D3D12_COMPARISON_FUNC_LESS_EQUAL",
        "stencilEnable": false
      },
      "staticSamplers": [
        {
          "shaderRegister": 0,
          "registerSpace": 0,
          "filter": "D3D12_FILTER_MIN_MAG_MIP_LINEAR",
          "addressU": "D3D12_TEXTURE_ADDRESS_MODE_WRAP",
          "addressV": "D3D12_TEXTURE_ADDRESS_MODE_WRAP",
          "addressW": "D3D12_TEXTURE_ADDRESS_MODE_WRAP",
          "comparisonFunc": "D3D12_COMPARISON_FUNC_ALWAYS",
          "shaderVisibility": "D3D12_SHADER_VISIBILITY_ALL"
        }
      ]
    },
    {
      "kind": "GraphicsVertex",
      "pipelineType": "Vertex",
      "shader": "Engine/Assets/Shaders/Builtin/Mesh/defaultMeshOutline.shader.json",
      "numRenderTargets": 1,
      "dynamicRenderTargetFormats": true,
      "dsvFormat": "DXGI_FORMAT_UNKNOWN",
      "topologyType": "D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE",
      "rasterizer": {
        "fillMode": "D3D12_FILL_MODE_SOLID",
        "cullMode": "D3D12_CULL_MODE_FRONT",
        "frontCounterClockwise": false,
        "depthClipEnable": true
      },
      "depthStencil": {
        "depthEnable": true,
        "depthWriteMask": "D3D12_DEPTH_WRITE_MASK_ZERO",
        "depthFunc": "D3D12_COMPARISON_FUNC_LESS_EQUAL",
        "stencilEnable": false
      },
      "staticSamplers": [
        {
          "shaderRegister": 0,
          "registerSpace": 0,
          "filter": "D3D12_FILTER_MIN_MAG_MIP_LINEAR",
          "addressU": "D3D12_TEXTURE_ADDRESS_MODE_WRAP",
          "addressV": "D3D12_TEXTURE_ADDRESS_MODE_WRAP",
          "addressW": "D3D12_TEXTURE_ADDRESS_MODE_WRAP",
          "comparisonFunc": "D3D12_COMPARISON_FUNC_ALWAYS",
          "shaderVisibility": "D3D12_SHADER_VISIBILITY_ALL"
        }
      ]
    }
  ]
}
```

### 8.2 Stencil silhouette 書き込み

追加:

```text
Assets/Pipelines/Builtin/Mesh/defaultMeshOutlineStencilWrite.pipeline.json
```

- 元メッシュの形状を描画する。
- RTV なし。
- SceneMain の depth をテストする。
- depth write は無効。
- depth pass で stencil を `REPLACE`。
- 既存 `defaultMeshZPrepass.shader.json` を再利用する。
- Vertex / Mesh Shader variant の両方を入れる。

```json
{
  "name": "DefaultMeshOutlineStencilWritePipeline",
  "variants": [
    {
      "kind": "GraphicsMesh",
      "pipelineType": "Mesh",
      "shader": "Engine/Assets/Shaders/Builtin/Mesh/defaultMeshZPrepass.shader.json",
      "numRenderTargets": 0,
      "dynamicRenderTargetFormats": false,
      "dsvFormat": "DXGI_FORMAT_UNKNOWN",
      "topologyType": "D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE",
      "requiresMeshShader": true,
      "rasterizer": {
        "fillMode": "D3D12_FILL_MODE_SOLID",
        "cullMode": "D3D12_CULL_MODE_BACK",
        "frontCounterClockwise": false,
        "depthClipEnable": true
      },
      "depthStencil": {
        "depthEnable": true,
        "depthWriteMask": "D3D12_DEPTH_WRITE_MASK_ZERO",
        "depthFunc": "D3D12_COMPARISON_FUNC_LESS_EQUAL",
        "stencilEnable": true,
        "stencilReadMask": 255,
        "stencilWriteMask": 255,
        "frontFace": {
          "stencilFailOp": "D3D12_STENCIL_OP_KEEP",
          "stencilDepthFailOp": "D3D12_STENCIL_OP_KEEP",
          "stencilPassOp": "D3D12_STENCIL_OP_REPLACE",
          "stencilFunc": "D3D12_COMPARISON_FUNC_ALWAYS"
        },
        "backFace": {
          "stencilFailOp": "D3D12_STENCIL_OP_KEEP",
          "stencilDepthFailOp": "D3D12_STENCIL_OP_KEEP",
          "stencilPassOp": "D3D12_STENCIL_OP_REPLACE",
          "stencilFunc": "D3D12_COMPARISON_FUNC_ALWAYS"
        }
      }
    },
    {
      "kind": "GraphicsVertex",
      "pipelineType": "Vertex",
      "shader": "Engine/Assets/Shaders/Builtin/Mesh/defaultMeshZPrepass.shader.json",
      "numRenderTargets": 0,
      "dynamicRenderTargetFormats": false,
      "dsvFormat": "DXGI_FORMAT_UNKNOWN",
      "topologyType": "D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE",
      "rasterizer": {
        "fillMode": "D3D12_FILL_MODE_SOLID",
        "cullMode": "D3D12_CULL_MODE_BACK",
        "frontCounterClockwise": false,
        "depthClipEnable": true
      },
      "depthStencil": {
        "depthEnable": true,
        "depthWriteMask": "D3D12_DEPTH_WRITE_MASK_ZERO",
        "depthFunc": "D3D12_COMPARISON_FUNC_LESS_EQUAL",
        "stencilEnable": true,
        "stencilReadMask": 255,
        "stencilWriteMask": 255,
        "frontFace": {
          "stencilFailOp": "D3D12_STENCIL_OP_KEEP",
          "stencilDepthFailOp": "D3D12_STENCIL_OP_KEEP",
          "stencilPassOp": "D3D12_STENCIL_OP_REPLACE",
          "stencilFunc": "D3D12_COMPARISON_FUNC_ALWAYS"
        },
        "backFace": {
          "stencilFailOp": "D3D12_STENCIL_OP_KEEP",
          "stencilDepthFailOp": "D3D12_STENCIL_OP_KEEP",
          "stencilPassOp": "D3D12_STENCIL_OP_REPLACE",
          "stencilFunc": "D3D12_COMPARISON_FUNC_ALWAYS"
        }
      }
    }
  ]
}
```

### 8.3 Stencil test 付き Hull 描画

追加:

```text
Assets/Pipelines/Builtin/Mesh/defaultMeshOutlineStencilTest.pipeline.json
```

`defaultMeshOutline.pipeline.json` と同様だが stencil test を `NOT_EQUAL` にする。書き込みマスクは `0`。

```json
"depthStencil": {
  "depthEnable": true,
  "depthWriteMask": "D3D12_DEPTH_WRITE_MASK_ZERO",
  "depthFunc": "D3D12_COMPARISON_FUNC_LESS_EQUAL",
  "stencilEnable": true,
  "stencilReadMask": 255,
  "stencilWriteMask": 0,
  "frontFace": {
    "stencilFailOp": "D3D12_STENCIL_OP_KEEP",
    "stencilDepthFailOp": "D3D12_STENCIL_OP_KEEP",
    "stencilPassOp": "D3D12_STENCIL_OP_KEEP",
    "stencilFunc": "D3D12_COMPARISON_FUNC_NOT_EQUAL"
  },
  "backFace": {
    "stencilFailOp": "D3D12_STENCIL_OP_KEEP",
    "stencilDepthFailOp": "D3D12_STENCIL_OP_KEEP",
    "stencilPassOp": "D3D12_STENCIL_OP_KEEP",
    "stencilFunc": "D3D12_COMPARISON_FUNC_NOT_EQUAL"
  }
}
```

残りの rasterizer、sampler、2 variant は stencil なし Hull と同じにする。

---

## 9. GPU 共有データ拡張

### 9.1 変更対象

```text
Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h
Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshBatchResources.h
Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshBatchResources.cpp
Assets/Shaders/Builtin/Mesh/defaultMesh.hlsli
Assets/Shaders/Builtin/Mesh/buildIndexedIndirectArgs.CS.hlsl
```

### 9.2 Outline GPU data

C++ 側へ追加する。16 バイト境界を維持する。

```cpp
struct MeshOutlineGPUData {
    Color4 color = Color4::Black();

    float width = 0.0f;
    float cameraZOffset = 0.0f;
    uint32_t expansionMode = 0;
    uint32_t widthMode = 0;

    uint32_t bakedNormalTextureIndex = UINT32_MAX;
    uint32_t outlineSamplerTextureIndex = UINT32_MAX;
    uint32_t flags = 0;
    uint32_t _pad0 = 0;
};
static_assert(sizeof(MeshOutlineGPUData) % 16 == 0);

static constexpr uint32_t kMeshOutlineFlagUseBakedNormal = 1u << 0;
static constexpr uint32_t kMeshOutlineFlagUseOutlineSampler = 1u << 1;
```

HLSL 側も同じ順序で定義する。

```hlsl
struct MeshOutlineGPUData {
    float4 color;

    float width;
    float cameraZOffset;
    uint expansionMode;
    uint widthMode;

    uint bakedNormalTextureIndex;
    uint outlineSamplerTextureIndex;
    uint flags;
    uint _pad0;
};
```

### 9.3 MeshInstanceData に outline index を追加

C++:

```cpp
struct MeshInstanceData {
    Matrix4x4 worldMatrix = Matrix4x4::Identity();

    uint32_t subMeshDataOffset = 0;
    uint32_t subMeshCount = 0;
    uint32_t flags = 0;
    uint32_t skinnedVertexOffset = 0;

    uint32_t outlineDataIndex = 0;
    uint32_t _outlinePad[3] = { 0, 0, 0 };
};
```

HLSL `defaultMesh.hlsli` と `buildIndexedIndirectArgs.CS.hlsl` の `MeshInstance` も完全に同じ順序へ変更する。

```hlsl
struct MeshInstance {
    float4x4 worldMatrix;

    uint subMeshDataOffset;
    uint subMeshCount;
    uint flags;
    uint skinnedVertexOffset;

    uint outlineDataIndex;
    uint3 _outlinePad;
};
```

理由:

- Vertex 経路の instance culling compute は `gMeshInstances` から可視インスタンスだけを `gVisibleMeshInstances` へコピーする。
- MS 経路も同じ instance data を参照する。
- `outlineDataIndex` を instance 自体へ含めれば、可視圧縮後も対応関係が崩れない。

### 9.4 SubMesh data に sourcePivot を追加

Position Scaling 用に追加する。

C++ `MeshSubMeshShaderData` の末尾:

```cpp
Vector3 sourcePivot = Vector3::AnyInit(0.0f);
float _outlinePad0 = 0.0f;
```

HLSL `SubMeshShaderData` の末尾:

```hlsl
float3 sourcePivot;
float _outlinePad0;
```

`MeshBatchResources::UploadBatchData()` で renderer の authoring data がある場合:

```cpp
data.sourcePivot = authoring.sourcePivot;
```

renderer authoring がない場合はゼロのままでよい。

### 9.5 Draw constants に culling 用 outline 値を追加

C++ `MeshDrawConstants` と HLSL の両方へ、同じ順番で追加する。

```cpp
uint32_t invertedHullOutlinePass = 0;
float outlineMaxModelExpansion = 0.0f;
float outlineMaxAbsCameraZOffset = 0.0f;
uint32_t outlineHasScreenPixelWidth = 0;
```

16-byte alignment を維持する。既存 `_pad` を必要に応じて調整する。

この値は per-instance 表現用ではなく、AS / instance-culling CS の **安全側 Bounds 膨張** 用。

---

## 10. MeshBatchResources の拡張

### 10.1 outline buffer を追加

`MeshBatchResources.h`:

```cpp
StructuredInstanceBuffer<MeshOutlineGPUData> outlineData_{ "gMeshOutlines" };
std::vector<MeshOutlineGPUData> outlineScratch_{};
```

アクセサ:

```cpp
D3D12_GPU_VIRTUAL_ADDRESS GetOutlineGPUAddress() const { return outlineData_.GetGPUAddress(); }
std::string_view GetOutlineBindingName() const { return outlineData_.GetBindingName(); }
```

`Init()`:

```cpp
outlineData_.Init(device, srvDescriptor);
```

`UploadBatchData()` の最初で:

```cpp
outlineScratch_.clear();
outlineScratch_.reserve(items.size());
outlineData_.EnsureCapacity(static_cast<uint32_t>((std::max)(items.size(), size_t(1))));
```

### 10.2 component 解決ヘルパー

`MeshBatchResources.cpp` の anonymous namespace:

```cpp
const Engine::InvertedHullOutlineComponent* ResolveOutline(const Engine::RenderItem* item) {
    if (!item || !item->world) {
        return nullptr;
    }
    return item->world->TryGetComponent<Engine::InvertedHullOutlineComponent>(item->entity);
}
```

### 10.3 instance ごとに GPU data を追加

`ResolveSRVIndex()` は既存の fallback texture 管理を使う。Baked Normal と Outline Sampler は **Linear** として解決する。

```cpp
MeshOutlineGPUData outlineGPU{};
if (const auto* outline = ResolveOutline(item)) {
    outlineGPU.color = outline->color;
    outlineGPU.width = (std::max)(0.0f, outline->width);
    outlineGPU.cameraZOffset = outline->cameraZOffset;
    outlineGPU.expansionMode = static_cast<uint32_t>(outline->expansionMode);
    outlineGPU.widthMode = static_cast<uint32_t>(outline->widthMode);

    if (outline->useBakedNormal && outline->bakedNormalTexture) {
        outlineGPU.flags |= kMeshOutlineFlagUseBakedNormal;
        outlineGPU.bakedNormalTextureIndex = ResolveSRVIndex(outline->bakedNormalTexture, false);
    }
    if (outline->useOutlineSampler && outline->outlineSamplerTexture) {
        outlineGPU.flags |= kMeshOutlineFlagUseOutlineSampler;
        outlineGPU.outlineSamplerTextureIndex = ResolveSRVIndex(outline->outlineSamplerTexture, false);
    }
}

instance.outlineDataIndex = static_cast<uint32_t>(outlineScratch_.size());
outlineScratch_.emplace_back(outlineGPU);
```

mesh instances と同じ数だけ必ず `outlineScratch_` を作る。component がない通常メッシュにもゼロ初期値を入れる。

GPU upload:

```cpp
outlineData_.Upload(outlineScratch_);
```

### 10.4 Draw constants 更新を UploadBatchData から分離

現状 `UploadBatchData()` の末尾近くで `MeshDrawConstants` を upload している。このままだと静的バッチキャッシュヒット時に、通常描画用の定数が Outline 描画へ残る可能性がある。

次を新規追加する。

```cpp
void UpdateDrawConstants(
    const RenderDrawContext& drawContext,
    const MeshGPUResource& gpuMesh);
```

`UploadBatchData()` は instance / submesh / outline / skinning data のアップロードを担当する。`MeshDrawConstants` は `UpdateDrawConstants()` で毎描画更新する。

`MeshBatchResources` 内に、upload 済み outline data から計算した保守的メトリクスを保持する。

```cpp
struct OutlineBatchMetrics {
    float maxModelExpansion = 0.0f;
    float maxAbsCameraZOffset = 0.0f;
    bool hasScreenPixelWidth = false;
};

OutlineBatchMetrics outlineMetrics_{};
```

`UploadBatchData()` で `outlineScratch_` 生成時に更新する。

```cpp
if (outlineGPU.widthMode == static_cast<uint32_t>(OutlineWidthMode::ScreenPixels)) {
    outlineMetrics_.hasScreenPixelWidth = true;
} else {
    outlineMetrics_.maxModelExpansion = (std::max)(outlineMetrics_.maxModelExpansion, outlineGPU.width);
}
outlineMetrics_.maxAbsCameraZOffset = (std::max)(
    outlineMetrics_.maxAbsCameraZOffset,
    std::abs(outlineGPU.cameraZOffset));
```

Outline Sampler は最大値 1 とみなせば安全側となる。

### 10.5 passName 判定

```cpp
bool IsHullOutlinePass(std::string_view passName) {
    return passName == "Outline" || passName == "OutlineStencilTest";
}
```

`OutlineStencilWrite` は元メッシュ形状なので Hull pass ではない。

### 10.6 UpdateDrawConstants の規則

```cpp
void MeshBatchResources::UpdateDrawConstants(
    const RenderDrawContext& drawContext,
    const MeshGPUResource& gpuMesh) {

    const bool hullOutline = IsHullOutlinePass(drawContext.passName);
    bool cullingEnabled = CanCullView(drawContext, gpuMesh);

    // ScreenPixels では近距離、投影、カメラ角度の影響を受ける。
    // 誤カリングを避けるため Hull のときだけ安全側で frustum culling を無効にする。
    if (hullOutline && outlineMetrics_.hasScreenPixelWidth) {
        cullingEnabled = false;
    }

    MeshDrawConstants drawConstants{};
    drawConstants.meshletCount = gpuMesh.meshletCount;
    drawConstants.subMeshCount = static_cast<uint32_t>(gpuMesh.subMeshes.size());
    drawConstants.instanceCount = instanceCount_;
    drawConstants.cullingEnabled = cullingEnabled ? 1u : 0u;
    drawConstants.packedMeshletVertexIndices = gpuMesh.usePackedMeshletVertexIndices ? 1u : 0u;

    // 背面法では通常メッシュの normal cone 判定を流用できない。
    // 線が小さくても見えるため contribution culling も無効化する。
    drawConstants.contributionCullingEnabled =
        (!hullOutline && cullingEnabled && drawContext.runtimeFeatures.useContributionCulling) ? 1u : 0u;
    drawConstants.normalConeCullingEnabled =
        (!hullOutline && cullingEnabled && drawContext.runtimeFeatures.useNormalConeCulling) ? 1u : 0u;

    drawConstants.meshBoundsCenter = gpuMesh.boundsCenter;
    drawConstants.meshBoundsRadius = gpuMesh.boundsRadius;
    drawConstants.contributionPixelThreshold = 0.5f;

    drawConstants.invertedHullOutlinePass = hullOutline ? 1u : 0u;
    drawConstants.outlineMaxModelExpansion = hullOutline ? outlineMetrics_.maxModelExpansion : 0.0f;
    drawConstants.outlineMaxAbsCameraZOffset = hullOutline ? outlineMetrics_.maxAbsCameraZOffset : 0.0f;
    drawConstants.outlineHasScreenPixelWidth = hullOutline && outlineMetrics_.hasScreenPixelWidth ? 1u : 0u;

    draw_.Upload(drawConstants);
}
```

### 10.7 キャッシュヒット時も毎回更新

`MeshRenderBackend::PrepareBatchResources()` の全経路で、resource が決定した後に必ず呼ぶ。

```cpp
resources->UpdateDrawConstants(context, *outPrepared.gpuMesh);
```

静的キャッシュヒット、スキニングキャッシュヒット、Billboard、初回 upload のすべてで実行する。

---

## 11. MeshRenderBackend の bind とキャッシュ

### 11.1 Outline SRV binding

変更対象:

```text
Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h
Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.cpp
```

slot:

```cpp
PipelineBindingCache::SlotID outlineSRVSlot_ = PipelineBindingCache::kInvalidSlot;
```

constructor:

```cpp
outlineSRVSlot_ = sharedBindCache_.AddSlot("gMeshOutlines", ShaderBindingKind::SRV);
```

`BindSharedResources()`:

```cpp
if (sharedBindCache_.Has(outlineSRVSlot_) && prepared.resources->GetOutlineGPUAddress() != 0) {
    RootBindingCommand::SetGraphicsSRV(
        commandList,
        sharedBindCache_.Get(outlineSRVSlot_),
        prepared.resources->GetOutlineGPUAddress(),
        {});
}
```

### 11.2 静的キャッシュ Hash へ outline 設定を混ぜる

`BuildStaticBatchHash()` へ、各 item の outline component の有無と全 authoring 値を追加する。

```cpp
const auto* outline = item->world
    ? item->world->TryGetComponent<InvertedHullOutlineComponent>(item->entity)
    : nullptr;

MixHash(h, outline ? 1ull : 0ull);
if (outline) {
    MixHash(h, outline->enabled ? 1ull : 0ull);
    MixBytes(h, &outline->width, sizeof(outline->width));
    MixBytes(h, &outline->color, sizeof(outline->color));
    MixHash(h, static_cast<uint64_t>(outline->expansionMode));
    MixHash(h, static_cast<uint64_t>(outline->widthMode));
    MixBytes(h, &outline->cameraZOffset, sizeof(outline->cameraZOffset));
    MixHash(h, outline->useBakedNormal ? 1ull : 0ull);
    MixHash(h, static_cast<uint64_t>(std::hash<AssetID>{}(outline->bakedNormalTexture)));
    MixHash(h, outline->useOutlineSampler ? 1ull : 0ull);
    MixHash(h, static_cast<uint64_t>(std::hash<AssetID>{}(outline->outlineSamplerTexture)));
    MixHash(h, outline->useStencil ? 1ull : 0ull);
}
```

`BuildBatchHash()` は現在 entity index / generation のみ。スキニングキャッシュは同一フレーム内だが、通常描画と outline 描画の resource 共有と editor 操作の整合性を明確にするため、outline component の authoring 値も同じように混ぜる。重複コードは anonymous namespace の `MixOutlineComponentHash()` へ切り出してよい。

### 11.3 `sourcePivot` upload

`MeshBatchResources.cpp` の submesh data 構築で:

```cpp
data.sourcePivot = authoring.sourcePivot;
```

これを忘れると Position Scaling が原点基準になり、編集済みサブメッシュで不正になる。

---

## 12. 共通 HLSL 拡張

### 12.1 新規ファイル

```text
Assets/Shaders/Builtin/Mesh/defaultMeshOutline.hlsli
Assets/Shaders/Builtin/Mesh/defaultMeshOutline.VS.hlsl
Assets/Shaders/Builtin/Mesh/defaultMeshOutline.AS.hlsl
Assets/Shaders/Builtin/Mesh/defaultMeshOutline.MS.hlsl
Assets/Shaders/Builtin/Mesh/defaultMeshOutline.PS.hlsl
Assets/Shaders/Builtin/Mesh/defaultMeshOutline.shader.json
```

### 12.2 共通 include の構成

`defaultMeshOutline.hlsli` は `defaultMesh.hlsli` を include し、outline 専用定義と関数を追加する。

```hlsl
#include "defaultMesh.hlsli"

static const uint OUTLINE_EXPANSION_NORMAL_DIRECTION = 0u;
static const uint OUTLINE_EXPANSION_POSITION_SCALING = 1u;
static const uint OUTLINE_WIDTH_MODEL_UNITS = 0u;
static const uint OUTLINE_WIDTH_SCREEN_PIXELS = 1u;
static const uint MESH_OUTLINE_FLAG_USE_BAKED_NORMAL = 1u << 0;
static const uint MESH_OUTLINE_FLAG_USE_OUTLINE_SAMPLER = 1u << 1;

SamplerState gOutlineSampler : register(s0);
StructuredBuffer<MeshOutlineGPUData> gMeshOutlines : register(t7, space1);

struct OutlineVertexOutput {
    float4 position : SV_Position;
    nointerpolation float4 color : COLOR0;
};
```

`register(t7, space1)` は既存割り当てと衝突しない番号を使用する。Root Binding は名前ベースなので、DXC Reflection で `gMeshOutlines` が正しく見えることをビルド時に確認する。既存スロットとの衝突がある場合は未使用 register へ変更する。

### 12.3 Texture sample helper

VS / MS では implicit derivative を使えないため `SampleLevel(..., 0.0f)` を使う。

```hlsl
float SampleOutlineWidthMultiplier(MeshOutlineGPUData outline, float2 uv) {
    if ((outline.flags & MESH_OUTLINE_FLAG_USE_OUTLINE_SAMPLER) == 0u ||
        outline.outlineSamplerTextureIndex == 0xFFFFFFFFu) {
        return 1.0f;
    }

    Texture2D<float4> tex = ResourceDescriptorHeap[
        NonUniformResourceIndex(outline.outlineSamplerTextureIndex)];
    return saturate(tex.SampleLevel(gOutlineSampler, uv, 0.0f).r);
}

float3 ResolveOutlineLocalNormal(
    MeshOutlineGPUData outline,
    MeshVertex vertex) {

    if ((outline.flags & MESH_OUTLINE_FLAG_USE_BAKED_NORMAL) == 0u ||
        outline.bakedNormalTextureIndex == 0xFFFFFFFFu) {
        return normalize(vertex.normal);
    }

    Texture2D<float4> tex = ResourceDescriptorHeap[
        NonUniformResourceIndex(outline.bakedNormalTextureIndex)];
    float3 encoded = tex.SampleLevel(gOutlineSampler, vertex.uv, 0.0f).xyz;
    float3 normal = encoded * 2.0f - 1.0f;
    return normalize(normal);
}
```

Baked Normal Texture は sRGB 無効の Linear texture とする。object/local-space normal を RGB に `[0, 1]` エンコードする。タンジェント空間 normal map と混同しない。

### 12.4 Camera Z Offset

現在 `MeshViewConstants` には `renderCameraPos` があるため、カメラからワールド頂点へ向かう方向へ押し込む。

```hlsl
float3 ApplyOutlineCameraZOffset(float3 worldPos, float cameraZOffset) {
    float3 fromCamera = worldPos - renderCameraPos;
    float len = length(fromCamera);
    if (len <= 0.00001f || abs(cameraZOffset) <= 0.00001f) {
        return worldPos;
    }
    return worldPos + fromCamera / len * cameraZOffset;
}
```

### 12.5 ModelUnits 膨張

法線方向:

```hlsl
localPos.xyz += localNormal * outline.width * widthMultiplier;
```

Position Scaling:

```hlsl
float3 fromPivot = localPos.xyz - subMesh.sourcePivot;
float len = length(fromPivot);
if (len > 0.00001f) {
    localPos.xyz += fromPivot / len * outline.width * widthMultiplier;
}
```

`PositionScaling` はピボットから放射方向へ同じ距離だけ押し出す。立方体などのハードエッジで法線方向の線が分断されるケースの代替。

### 12.6 ScreenPixels 膨張

画面上の線幅を一定にする場合、ワールド空間の押し出し距離ではなく clip / NDC 上で XY offset を加える。

手順:

1. 元頂点を world へ変換。
2. Camera Z Offset を適用。
3. Local normal または pivot 放射方向を world direction へ変換。
4. `worldPos` と `worldPos + worldDirection` を clip へ変換。
5. NDC XY 差分を正規化。
6. `2 * widthPixels / viewSize` を掛ける。
7. clip XY へ `ndcOffset * clip.w` を加える。

```hlsl
float4 ApplyScreenPixelOutlineOffset(
    float3 worldPos,
    float3 worldDirection,
    float widthPixels) {

    float4 clip = mul(float4(worldPos, 1.0f), viewProjection);
    float4 dirClip = mul(float4(worldPos + worldDirection, 1.0f), viewProjection);

    if (abs(clip.w) <= 0.00001f || abs(dirClip.w) <= 0.00001f) {
        return clip;
    }

    float2 ndc = clip.xy / clip.w;
    float2 dirNdc = dirClip.xy / dirClip.w;
    float2 projectedDir = dirNdc - ndc;
    float projectedLen = length(projectedDir);
    if (projectedLen <= 0.00001f) {
        return clip;
    }

    float2 safeViewSize = max(viewSize, float2(1.0f, 1.0f));
    float2 pixelToNdc = 2.0f / safeViewSize;
    float2 ndcOffset = normalize(projectedDir) * widthPixels * pixelToNdc;
    clip.xy += ndcOffset * clip.w;
    return clip;
}
```

非一様な viewSize では X/Y のピクセル変換値を別々に適用する。

### 12.7 共通 Build 関数

VS と MS が同一ロジックを使うよう、膨張計算を共通関数へまとめる。

```hlsl
OutlineVertexOutput BuildOutlineVertex(
    uint instanceID,
    uint localSubMeshIndex,
    MeshVertex vertex,
    float4x4 worldMatrix) {

    MeshInstance instance = gMeshInstances[instanceID];
    MeshOutlineGPUData outline = gMeshOutlines[instance.outlineDataIndex];
    SubMeshShaderData subMesh = GetInstanceSubMesh(instanceID, localSubMeshIndex);

    float widthMultiplier = SampleOutlineWidthMultiplier(outline, vertex.uv);
    float3 localNormal = ResolveOutlineLocalNormal(outline, vertex);

    float4 localPos = vertex.position;
    float3 localDirection = localNormal;

    if (outline.expansionMode == OUTLINE_EXPANSION_POSITION_SCALING) {
        float3 fromPivot = localPos.xyz - subMesh.sourcePivot;
        if (length(fromPivot) > 0.00001f) {
            localDirection = normalize(fromPivot);
        }
    }

    if (outline.widthMode == OUTLINE_WIDTH_MODEL_UNITS) {
        localPos.xyz += localDirection * outline.width * widthMultiplier;
    }

    float3 worldPos = mul(localPos, worldMatrix).xyz;
    worldPos = ApplyOutlineCameraZOffset(worldPos, outline.cameraZOffset);

    OutlineVertexOutput output;
    if (outline.widthMode == OUTLINE_WIDTH_SCREEN_PIXELS) {
        float3 worldDirection = normalize(mul(localDirection, (float3x3)worldMatrix));
        output.position = ApplyScreenPixelOutlineOffset(
            worldPos,
            worldDirection,
            outline.width * widthMultiplier);
    } else {
        output.position = mul(float4(worldPos, 1.0f), viewProjection);
    }
    output.color = outline.color;
    return output;
}
```

非一様スケール下で方向変換をより厳密にする場合、normal-direction のみ inverse-transpose へ分ける。まず既存 engine の行列規約 (`mul(rowVector, matrix)`) を維持し、見た目とビルドを確認する。Position Scaling の方向は通常の `(float3x3)worldMatrix` でよい。

---

## 13. Vertex Shader 経路

### 13.1 `defaultMeshOutline.VS.hlsl`

```hlsl
#include "defaultMeshOutline.hlsli"

OutlineVertexOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    MeshVertex vertex = LoadMeshVertex(instanceID, vertexID);
    uint localSubMeshIndex = gVertexSubMeshIndices[vertexID];
    float4x4 worldMatrix = GetInstanceSubMeshWorldMatrix(instanceID, localSubMeshIndex);
    return BuildOutlineVertex(instanceID, localSubMeshIndex, vertex, worldMatrix);
}
```

Vertex 経路は既存の instance culling compute を経由するため、後述の `buildIndexedIndirectArgs.CS.hlsl` 修正が必須。

---

## 14. Mesh Shader 経路

### 14.1 `defaultMeshOutline.AS.hlsl`

既存 `defaultMesh.AS.hlsl` と同じ形で、`defaultMeshOutline.hlsli` を include する。`IsMeshletVisible()` は共通 `defaultMesh.hlsli` 側の outline-aware culling 修正を使う。

```hlsl
#include "defaultMeshOutline.hlsli"

groupshared MeshDispatchPayload payload;

[numthreads(32, 1, 1)]
void main(uint groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID) {
    const uint meshletIndex = groupID.x * 32u + groupThreadID;
    const uint instanceIndex = groupID.y;
    const bool visible = meshletIndex < meshletCount && IsMeshletVisible(meshletIndex, instanceIndex);

    const uint visibleOffset = WavePrefixCountBits(visible);
    const uint visibleCount = WaveActiveCountBits(visible);
    if (visible) {
        payload.meshletIndices[visibleOffset] = meshletIndex;
        payload.instanceIndices[visibleOffset] = instanceIndex;
    }
    GroupMemoryBarrierWithGroupSync();
    DispatchMesh(visibleCount, 1, 1, payload);
}
```

### 14.2 `defaultMeshOutline.MS.hlsl`

既存 Mesh Shader と同様に primitive と vertex を出力し、vertex の position/color だけ outline 用関数で生成する。

```hlsl
#include "defaultMeshOutline.hlsli"

groupshared float4x4 gMeshletWorldMatrix;

[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void main(
    uint groupThreadID : SV_GroupThreadID,
    uint3 groupID : SV_GroupID,
    in payload MeshDispatchPayload payload,
    out vertices OutlineVertexOutput outVerts[64],
    out indices uint3 outTris[124]) {

    const uint meshletIndex = payload.meshletIndices[groupID.x];
    const uint instanceIndex = payload.instanceIndices[groupID.x];
    const MeshletDrawDesc meshlet = gMeshlets[meshletIndex];
    SetMeshOutputCounts(meshlet.vertexCount, meshlet.primitiveCount);

    const uint localSubMeshIndex = meshlet.subMeshIndex;
    if (groupThreadID == 0) {
        gMeshletWorldMatrix = GetInstanceSubMeshWorldMatrix(instanceIndex, localSubMeshIndex);
    }
    GroupMemoryBarrierWithGroupSync();

    if (groupThreadID < meshlet.primitiveCount) {
        outTris[groupThreadID] = UnpackPrimitiveIndex(
            gMeshletPrimitiveIndices[meshlet.primitiveOffset + groupThreadID]);
    }

    if (groupThreadID < meshlet.vertexCount) {
        uint vertexIndex = LoadMeshletVertexIndex(meshlet.vertexOffset + groupThreadID);
        MeshVertex vertex = LoadMeshVertex(instanceIndex, vertexIndex);
        outVerts[groupThreadID] = BuildOutlineVertex(
            instanceIndex,
            localSubMeshIndex,
            vertex,
            gMeshletWorldMatrix);
    }
}
```

### 14.3 `defaultMeshOutline.PS.hlsl`

```hlsl
#include "defaultMeshOutline.hlsli"

float4 main(OutlineVertexOutput input) : SV_TARGET0 {
    return input.color;
}
```

### 14.4 Shader asset

```json
{
  "name": "DefaultMeshOutline",
  "stages": [
    {
      "stage": "VS",
      "file": "Builtin/Mesh/defaultMeshOutline.VS.hlsl",
      "entry": "main",
      "profile": "vs_6_6"
    },
    {
      "stage": "AS",
      "file": "Builtin/Mesh/defaultMeshOutline.AS.hlsl",
      "entry": "main",
      "profile": "as_6_6"
    },
    {
      "stage": "MS",
      "file": "Builtin/Mesh/defaultMeshOutline.MS.hlsl",
      "entry": "main",
      "profile": "ms_6_6"
    },
    {
      "stage": "PS",
      "file": "Builtin/Mesh/defaultMeshOutline.PS.hlsl",
      "entry": "main",
      "profile": "ps_6_6"
    }
  ]
}
```

VS で `ResourceDescriptorHeap` を使用するため、既存 shader compiler / root signature generator が Shader Model 6.6 の direct heap indexing に対応していることを確認する。必要なら VS profile を `vs_6_6` にする。Mesh / PS は既に 6.6 を利用している。

---

## 15. Culling の修正

### 15.1 なぜ必須か

Hull は元形状より外へ膨張する。元 Bounds のままでは画面端でアウトラインだけが突然消える。

Mesh Shader 経路では `defaultMesh.hlsli::IsMeshletVisible()`、Vertex 経路では `buildIndexedIndirectArgs.CS.hlsl::CalcInstanceCullBounds()` が別々に判定するため、両方を修正する。

### 15.2 Meshlet culling

`defaultMesh.hlsli` の `IsMeshletVisible()`:

```hlsl
float radius = bounds.radius * GetMatrixMaxScale(worldMatrix);
```

を、outline pass で安全側へ膨張する。

```hlsl
float localRadius = bounds.radius;
if (invertedHullOutlinePass != 0u) {
    localRadius += outlineMaxModelExpansion;
}
float radius = localRadius * GetMatrixMaxScale(worldMatrix);
if (invertedHullOutlinePass != 0u) {
    radius += outlineMaxAbsCameraZOffset;
}
```

Normal cone culling は C++ `UpdateDrawConstants()` 側で Hull のとき必ず無効にする。

ScreenPixels が一件でも含まれる Hull batch は C++ 側で `cullingEnabled = false` とする。投影ベースの幅を安全に bounds へ変換せず、誤カリングしないことを優先する。

### 15.3 Vertex path instance culling

`buildIndexedIndirectArgs.CS.hlsl` の CBuffer と struct を C++ と一致させる。

`CalcInstanceCullBounds()` 内で base radius と submesh radius の両方へ膨張を反映する。

```hlsl
float ResolveOutlineCullLocalExpansion() {
    return invertedHullOutlinePass != 0u ? outlineMaxModelExpansion : 0.0f;
}

float ResolveOutlineCullWorldExtra() {
    return invertedHullOutlinePass != 0u ? outlineMaxAbsCameraZOffset : 0.0f;
}
```

例:

```hlsl
const float outlineLocal = ResolveOutlineCullLocalExpansion();
const float outlineWorldExtra = ResolveOutlineCullWorldExtra();

radius = (meshBoundsRadius + outlineLocal) * GetMatrixMaxScale(instance.worldMatrix) + outlineWorldExtra;
```

submesh loop:

```hlsl
float localRadius = (meshBoundsRadius + outlineLocal) * GetMatrixMaxScale(subMesh.localMatrix);
float worldRadius = localRadius * GetMatrixMaxScale(instance.worldMatrix) + outlineWorldExtra;
```

ScreenPixels の場合は C++ から `cullingEnabled = 0` が渡るため、CS は全 instance を visible とする。

---

## 16. Pipeline variant 選択

Outline material の `preferredVariant` は `GraphicsMesh` とする。既存 `ResolveBestVariant()` が runtime features に応じて Mesh Shader 対応環境では Mesh variant、非対応環境や preview の `forceVertexMeshVariant` では Vertex variant を選ぶ。

重要:

- `InvertedHullOutlinePass` から `forceVertexMeshVariant = true` を渡してはいけない。
- Preview やランタイム設定により Vertex 経路へ切り替わる既存挙動は維持する。
- `OutlineStencilWrite` も `defaultMeshZPrepass.shader.json` に含まれる VS / AS / MS を使用し、両経路で動作する。
- `OutlineStencilTest` は outline VS / AS / MS を使用する。

---

## 17. Inspector 対応

### 17.1 新規ファイル

```text
Editor/UI/Inspectors/Builtin/Render/InvertedHullOutlineInspectorDrawer.h
Editor/UI/Inspectors/Builtin/Render/InvertedHullOutlineInspectorDrawer.cpp
```

既存 `SerializedComponentInspectorDrawer<T>` を継承する。

```cpp
class InvertedHullOutlineInspectorDrawer :
    public SerializedComponentInspectorDrawer<InvertedHullOutlineComponent> {
public:
    InvertedHullOutlineInspectorDrawer() :
        SerializedComponentInspectorDrawer("Inverted Hull Outline", "InvertedHullOutline") {}

protected:
    void DrawFields(const EditorPanelContext& context,
        ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
};
```

実際の virtual method 名は既存 drawer のヘッダへ合わせる。

### 17.2 表示項目

既存 `InspectorDrawerCommon::DrawEnumComboField()`、`DrawCheckboxField()`、`MyGUI::AssetReferenceField()`、Color edit、DragFloat の既存書き方へ合わせる。

```text
有効                   enabled
色                     color
膨張幅                 width
幅モード               widthMode
膨張方式               expansionMode
Camera Z Offset        cameraZOffset
Baked Normal を使う    useBakedNormal
Baked Normal Texture   bakedNormalTexture (Texture asset)
部位別幅を使う         useOutlineSampler
Outline Sampler        outlineSamplerTexture (Texture asset)
Stencil 抑制           useStencil
```

`widthMode == ScreenPixels` のとき Inspector のラベルや tooltip で単位が px であることを示す。

`useBakedNormal == false` のとき texture 編集欄を disabled にするか、表示したままでもよい。`useOutlineSampler` も同様。

### 17.3 Add Component メニュー

変更対象:

```text
Editor/UI/Panels/Builtin/InspectorPanel.cpp
```

`kOptionalComponentMenuEntries` の配列サイズを 15 から 16 に変え、Rendering カテゴリへ追加する。

```cpp
{ "Inverted Hull Outline", "InvertedHullOutline", "Rendering" },
```

include と constructor registration を追加する。

```cpp
componentDrawers_.emplace_back(std::make_unique<InvertedHullOutlineInspectorDrawer>());
```

---

## 18. Visual Studio project file

追加した C++ `.h` / `.cpp` を次へ登録する。

```text
NEMEngine.vcxproj
NEMEngine.vcxproj.filters
```

既存のフィルタ階層に合わせる。

HLSL / JSON は既存 project file で明示列挙している場合のみ同様に登録する。列挙していない場合は不要。

---

## 19. ファイル一覧

### 19.1 新規追加

```text
Core/World/Components/Rendering/InvertedHullOutlineComponent.h
Core/World/Components/Rendering/InvertedHullOutlineComponent.cpp

Core/Rendering/Renderer/RenderPath/Passes/InvertedHullOutlinePass.h
Core/Rendering/Renderer/RenderPath/Passes/InvertedHullOutlinePass.cpp

Editor/UI/Inspectors/Builtin/Render/InvertedHullOutlineInspectorDrawer.h
Editor/UI/Inspectors/Builtin/Render/InvertedHullOutlineInspectorDrawer.cpp

Assets/Materials/Builtin/Mesh/defaultMeshOutline.material.json

Assets/Pipelines/Builtin/Mesh/defaultMeshOutline.pipeline.json
Assets/Pipelines/Builtin/Mesh/defaultMeshOutlineStencilWrite.pipeline.json
Assets/Pipelines/Builtin/Mesh/defaultMeshOutlineStencilTest.pipeline.json

Assets/Shaders/Builtin/Mesh/defaultMeshOutline.hlsli
Assets/Shaders/Builtin/Mesh/defaultMeshOutline.VS.hlsl
Assets/Shaders/Builtin/Mesh/defaultMeshOutline.AS.hlsl
Assets/Shaders/Builtin/Mesh/defaultMeshOutline.MS.hlsl
Assets/Shaders/Builtin/Mesh/defaultMeshOutline.PS.hlsl
Assets/Shaders/Builtin/Mesh/defaultMeshOutline.shader.json
```

### 19.2 変更

```text
Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.cpp

Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h
Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.cpp
Core/Rendering/Renderer/Passes/RenderItemBatchDispatcher.h
Core/Rendering/Renderer/Passes/RenderItemBatchDispatcher.cpp

Core/Rendering/Materials/MaterialResolver.h
Core/Rendering/Materials/MaterialResolver.cpp
Core/Rendering/Assets/RenderPipelineAsset.cpp

Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h
Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshBatchResources.h
Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshBatchResources.cpp
Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h
Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.cpp

Assets/Shaders/Builtin/Mesh/defaultMesh.hlsli
Assets/Shaders/Builtin/Mesh/buildIndexedIndirectArgs.CS.hlsl

Editor/UI/Panels/Builtin/InspectorPanel.cpp

NEMEngine.vcxproj
NEMEngine.vcxproj.filters
```

---

## 20. 一括実装の作業順序

これは中間停止ポイントではない。最後まで継続して実装する。

1. リポジトリルートで `git status --short` を取得し、既存ユーザー変更を把握する。ユーザー変更を上書きしない。
2. 新規 component と JSON 変換を追加する。
3. GPU struct を C++ / HLSL / culling CS で同時更新する。レイアウト不一致を残さない。
4. `MeshBatchResources` へ outline buffer、texture 解決、metrics、毎描画 draw constants 更新を追加する。
5. `MeshRenderBackend` へ SRV bind、cache hash、default material pass 解決を追加する。
6. 外部 DSV 対応を helper / dispatcher へ追加し、既存 `depthOnly` 引き渡しバグを修正する。
7. pipeline JSON parser の stencil 読み込みと書き戻しを拡張する。
8. Outline shader の VS / AS / MS / PS と pipeline / material JSON を追加する。
9. `InvertedHullOutlinePass` を追加し、regular / stencil の両分岐を実装する。
10. FixedRenderPath へパスを挿入する。
11. Inspector drawer、Add Component menu、project files を更新する。
12. 全 HLSL の struct レイアウトを再確認する。
13. ビルドする。
14. 可能なら実行し、VS / MS の両経路を切り替えて検証する。
15. `git diff --stat` と主要差分を確認し、実装漏れを修正する。

---

## 21. ビルドと検証

### 21.1 ビルド

リポジトリ構造に合わせ、利用可能な Visual Studio Developer Command Prompt または MSBuild でビルドする。

候補:

```bat
cd /d C:\Users\k023g\school\NEMProjects\NEMEngine
msbuild NEMEngine.vcxproj /m /p:Configuration=Develop /p:Platform=x64
```

親ディレクトリに `.sln` がある場合は、その solution の engine / editor 起動対象をビルドしてよい。実在する Configuration 名は project file を確認して選ぶ。

### 21.2 Shader compile

最低限、次がコンパイルされることを確認する。

```text
defaultMeshOutline.VS.hlsl
defaultMeshOutline.AS.hlsl
defaultMeshOutline.MS.hlsl
defaultMeshOutline.PS.hlsl
buildIndexedIndirectArgs.CS.hlsl
defaultMeshZPrepass.VS.hlsl
defaultMeshZPrepass.AS.hlsl
defaultMeshZPrepass.MS.hlsl
```

`ResourceDescriptorHeap` を VS / MS で利用するため、Shader Model 6.6 と root signature の direct indexing 対応を確認する。

### 21.3 動作確認ケース

| ケース | 確認内容 |
|---|---|
| Sphere + ModelUnits + NormalDirection | 基本外周線 |
| Cube + NormalDirection | ハードエッジの挙動 |
| Cube + PositionScaling | 角の分断が改善されること |
| Character mesh | 複雑形状 |
| Skinned character | アニメーション追従 |
| SceneView preview | Vertex variant で表示 |
| GameView Mesh Shader ON | AS/MS 経路で表示 |
| Mesh Shader OFF | VS 経路で表示 |
| Non-uniform scale | 破綻がないこと |
| Camera 接近・離脱 + ScreenPixels | 画面上の線幅がおおむね一定 |
| Camera Z Offset | 正負の調整が反映 |
| Baked Normal texture | 法線差し替えが反映 |
| Outline sampler texture | 黒部分で線が消え、白部分で最大幅 |
| useStencil = false | 基本 Hull 描画 |
| useStencil = true | シルエット stencil 抑制が動作 |
| 2 outlined entities overlap | stencil 有効時の重なり抑制 |
| 画面端 | Bounds 誤カリングがない |
| Inspector で width / color を変更 | 静的 cache が更新され即時反映 |
| Component 削除 | 次フレームで線が消える |
| Reflection ON / OFF | SceneFinal 上で線が安定 |

### 21.4 CPU/HLSL レイアウト確認

次の struct は C++ と HLSL のフィールド順、型、padding を一行ずつ比較する。

```text
MeshDrawConstants
MeshInstanceData / MeshInstance
MeshSubMeshShaderData / SubMeshShaderData
MeshOutlineGPUData
```

`static_assert(sizeof(...) % 16 == 0)` を追加できる構造体には追加する。

---

## 22. 完了条件

以下をすべて満たすまで終了しない。

- [ ] `InvertedHullOutlineComponent` を Add Component から追加できる。
- [ ] component の JSON 保存とロードが動作する。
- [ ] component がない mesh は表示が変わらない。
- [ ] component がある Opaque mesh だけに追加 Hull が描かれる。
- [ ] Hull は `SceneFinal RTV + SceneMain DSV` で描画される。
- [ ] Outline pass は reflection 後、transparent 前に入っている。
- [ ] Vertex Shader 経路が動作する。
- [ ] Mesh Shader + Amplification Shader 経路が動作する。
- [ ] Outline で常時 Vertex variant を強制していない。
- [ ] NormalDirection が動作する。
- [ ] PositionScaling が動作する。
- [ ] ModelUnits が動作する。
- [ ] ScreenPixels が動作する。
- [ ] Camera Z Offset が動作する。
- [ ] Baked Normal Texture が動作する。
- [ ] Outline Sampler Texture が動作する。
- [ ] `useStencil` の false / true が両方動作する。
- [ ] Outline Hull の depth write は無効。
- [ ] Outline Hull の front face culling が有効。
- [ ] MS の normal cone culling は Hull で無効。
- [ ] VS / MS の culling bounds が outline 分だけ安全側へ広がる。
- [ ] ScreenPixels Hull の誤カリング回避が入っている。
- [ ] 静的 cache hit でも draw constants が毎描画更新される。
- [ ] Inspector 変更が静的 cache hash に反映される。
- [ ] BakedNormal / OutlineSampler の fallback texture 状態が既存 cache 方針へ反映される。
- [ ] 既存 ZPrepass の `depthOnly` 引き渡しバグが修正される。
- [ ] 追加 C++ ファイルが `.vcxproj` と `.filters` に登録される。
- [ ] ビルドエラーがない。
- [ ] 新規コードに TODO やダミー実装が残っていない。

---

## 23. 実装完了後の報告形式

最後に、Claude Code CLI は次を簡潔に報告する。

```text
1. 変更したファイル一覧
2. 実装した機能一覧
3. Vertex Shader 経路の実装箇所
4. Mesh Shader / Amplification Shader 経路の実装箇所
5. Stencil 分岐の実装箇所
6. 外部 DSV 対応と depthOnly バグ修正箇所
7. 実行したビルドコマンド
8. ビルド結果
9. 実行時に確認できた項目
10. 残課題がある場合、その正確な理由
```

ビルドまたは実行環境の制約で確認できない項目がある場合も、未確認であることを明示し、実装済みと検証済みを混同しない。
