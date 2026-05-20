# NEMEngine05 RenderPath NodeGraph Editor 実装指示書

## 目的

NEMEngine05 に、今後の複数ツールで共通利用できる **汎用ノードグラフ基盤** と、その最初の実用ツールである **シーン描画パス編集ツール / RenderPath Graph Editor** を実装してください。

今回の最終目標は、現在 `.scene.json` の `Header.passOrder` で管理しているシーン描画パス順序を、Editor 上の Unity 風ノード UI で表示・編集・検証・保存できるようにすることです。

ただし、描画実行そのものは既存の `ScenePassExecutor` / `PostProcessExecutor` / Renderer 側の仕組みを使い続けてください。ノードエディタは Runtime の実行システムではなく、あくまで **Editor 上の authoring tool** として実装してください。

---

## 対象ソース

最新の対象ソースは以下です。

```text
NEMEngine05.zip
```

作業対象は基本的に以下です。

```text
C:\Users\k023g\school\NEMProjects\NEMEngine\Project
```

---

## 今回作るもの

今回の中心は以下です。

```text
1. 汎用ノードグラフ基盤
2. Unity 風ノード描画 View
3. RenderPath Graph Document
4. RenderPath Graph Importer
5. RenderPath Graph Validator
6. RenderPath Graph Compiler
7. RenderPath Graph Editor Tool
8. .scene.json / Header.passOrder との相互変換
9. PostProcess / TemporaryTarget / Bloom へ拡張できる基盤
```

将来的には、この汎用ノード基盤を以下のツールにも使い回せるようにしてください。

```text
- シーン描画パス編集ツール
- シーン処理順序 / Scene Flow Editor
- シーン遷移演出作成ツール
- エフェクト作成ツール 2D / 3D
- シェーダーグラフ
- Animation Controller
- Behavior Tree
```

---

## 最重要方針

### 1. Renderer の挙動を壊さない

今回のツールは Editor 用です。

以下の既存挙動を壊さないでください。

```text
- Clear
- DepthPrepass
- Draw
- Compute
- RenderScene
- Blit
- Raytracing
- FullscreenCopy
- LightCulling
- PostProcess
- SceneMain / SceneFinal / SceneColorFinal / View
- Editor Viewport
- Runtime 実行
- 既存 .scene.json の読み込み
```

特に、ノードエディタの導入によって `ScenePassExecutor` の実行仕様を変えないでください。

---

### 2. ノードは Runtime 実行データにしない

悪い設計:

```text
NodeGraphTool が ScenePassExecutor を直接呼ぶ
NodeGraphTool が RenderTarget を直接作成する
NodeGraphTool が PostProcessExecutor を直接呼ぶ
NodeGraphTool が毎フレーム Renderer の処理順を直接書き換える
GraphDocument を Runtime の描画実行データとして扱う
```

正しい設計:

```text
RenderPathGraphTool
  ↓ 編集
RenderPathGraphDocument / GraphDocument
  ↓ 検証
RenderPathGraphValidator
  ↓ 変換
RenderPathGraphCompiler
  ↓ 反映
SceneHeader.passOrder / ScenePassDesc
  ↓ 既存 Runtime 処理
ScenePassExecutor
```

ノードエディタは **SceneHeader.passOrder を編集するための Editor UI** です。

---

### 3. 責務を明確に分ける

1つのクラスに責務を詰め込まないでください。

悪い例:

```text
RenderPathGraphTool.cpp に以下を全部入れる
- imgui-node-editor の描画
- GraphDocument の管理
- .scene.json の読み書き
- passOrder 変換
- Validation
- Node style
- Asset drag & drop
- Save / Load
- Undo / Redo
```

良い例:

```text
GraphDocument
  - 汎用ノードデータを保持

NodeGraphView
  - imgui-node-editor を使って GraphDocument を描画

NodeGraphStyle
  - Unity 風ノードの見た目を管理

GraphSerializer
  - 汎用 GraphDocument の JSON 保存 / 読み込み

RenderPathGraphImporter
  - SceneHeader.passOrder から GraphDocument を生成

RenderPathGraphValidator
  - RenderPath として正しいか検証

RenderPathGraphCompiler
  - GraphDocument から ScenePassDesc / passOrder を生成

RenderPathGraphTool
  - EditorTool として UI をまとめる
  - 各クラスに処理を委譲する
```

---

### 4. グラフ編集本体は Editor/Tools/Builtin/ に置く

ユーザーが開く実際の Editor Tool は以下に置いてください。

```text
Engine/Editor/Tools/Builtin/RenderPathGraph/
  RenderPathGraphTool.h
  RenderPathGraphTool.cpp
```

ただし、機能すべてを `Editor/Tools/Builtin/` に詰め込まないでください。

汎用グラフ基盤、View、Serializer、RenderPath 変換系は適切なフォルダに分離してください。

---

### 5. Unity 風の見た目を目指す

見た目は Unity の Shader Graph / Visual Scripting / GraphView 系に寄せてください。

目標:

```text
- ダークテーマ
- 角丸ノード
- タイトルバー
- 種類別アクセントカラー
- 入力ピンは左、出力ピンは右
- 実行順ピンとデータピンを区別
- ノード内に主要プロパティを表示
- エラーがあるノードは下部に警告バーを表示
- Disabled ノードは半透明
- 選択中ノードは枠線強調
- 右クリック Add Node メニュー
- ノード検索 / フィルタできる拡張性
```

---

## 推奨フォルダ構成

### 汎用グラフ基盤

```text
Engine/Editor/Graph/
  GraphId.h
  GraphTypes.h
  GraphPin.h
  GraphNode.h
  GraphLink.h
  GraphDocument.h
  GraphDocument.cpp
  GraphNodeDefinition.h
  GraphNodeDefinition.cpp
  GraphNodeRegistry.h
  GraphNodeRegistry.cpp
  GraphSerializer.h
  GraphSerializer.cpp
  GraphValidationTypes.h
```

### 汎用ノード View

```text
Engine/Editor/Graph/View/
  NodeGraphContext.h
  NodeGraphContext.cpp
  NodeGraphView.h
  NodeGraphView.cpp
  NodeGraphStyle.h
  NodeGraphStyle.cpp
  NodeGraphDrawUtils.h
  NodeGraphDrawUtils.cpp
  NodeGraphInteraction.h
  NodeGraphInteraction.cpp
```

### RenderPath 専用グラフ

```text
Engine/Editor/RenderPathGraph/
  RenderPathGraphTypes.h
  RenderPathGraphDocument.h
  RenderPathGraphDocument.cpp
  RenderPathGraphNodeFactory.h
  RenderPathGraphNodeFactory.cpp
  RenderPathGraphImporter.h
  RenderPathGraphImporter.cpp
  RenderPathGraphValidator.h
  RenderPathGraphValidator.cpp
  RenderPathGraphCompiler.h
  RenderPathGraphCompiler.cpp
  RenderPathGraphSerializer.h
  RenderPathGraphSerializer.cpp
  RenderPathGraphAssetDragDrop.h
  RenderPathGraphAssetDragDrop.cpp
```

### Builtin Tool 本体

```text
Engine/Editor/Tools/Builtin/RenderPathGraph/
  RenderPathGraphTool.h
  RenderPathGraphTool.cpp
```

### 登録

```text
Engine/Editor/Tools/Builtin/BuiltinEditorTools.cpp
Engine/Editor/Tools/Builtin/BuiltinEditorTools.h
```

---

## 汎用グラフデータ設計

### GraphId

```cpp
using GraphId = uint64_t;
```

GraphId はノード、ピン、リンクすべてで使えるようにしてください。

ID 生成は `GraphDocument` または専用の `GraphIdGenerator` に閉じ込めてください。

---

### GraphValueType

RenderPath 以外にも使えるように、値の種類を抽象化してください。

```cpp
enum class GraphValueType {
    Unknown,

    Flow,
    Texture2D,
    Texture2DUAV,
    DepthTexture,
    RenderTarget,
    Buffer,
    View,

    Float,
    Float2,
    Float3,
    Float4,
    Int,
    UInt,
    Bool,
    String,

    Asset,
    MaterialAsset,
    SceneAsset,
    TransitionEffectAsset,
    ShaderAsset,
};
```

RenderPath では特に以下を使います。

```text
Flow
Texture2D
Texture2DUAV
DepthTexture
RenderTarget
View
MaterialAsset
```

---

### GraphPin

```cpp
enum class GraphPinKind {
    Input,
    Output,
};

struct GraphPin {
    GraphId id = 0;
    GraphId nodeId = 0;
    GraphPinKind kind = GraphPinKind::Input;
    GraphValueType valueType = GraphValueType::Unknown;
    std::string name;
    bool required = false;
    bool allowMultipleLinks = false;
};
```

---

### GraphNode

最初からノード種類ごとの C++ 派生クラスを大量に作らないでください。

悪い例:

```cpp
class ClearPassNode : public GraphNode {};
class DrawPassNode : public GraphNode {};
class PostProcessNode : public GraphNode {};
class BlitNode : public GraphNode {};
```

推奨:

```cpp
struct GraphNode {
    GraphId id = 0;
    std::string type;
    std::string displayName;

    ImVec2 position{0.0f, 0.0f};
    ImVec2 size{0.0f, 0.0f};

    bool enabled = true;
    bool collapsed = false;

    std::vector<GraphPin> inputs;
    std::vector<GraphPin> outputs;

    nlohmann::json properties;

    std::vector<std::string> validationMessages;
};
```

`type` の例:

```text
RenderPath.Clear
RenderPath.DepthPrepass
RenderPath.Draw
RenderPath.Compute
RenderPath.PostProcess
RenderPath.Blit
RenderPath.Raytracing
RenderPath.RenderScene
RenderPath.TemporaryTarget
RenderPath.RenderTarget
RenderPath.View

SceneFlow.Scene
SceneFlow.Transition
SceneFlow.Branch

Transition.Fade
Transition.Wipe
Transition.PostProcess

Effect.ParticleEmitter
Effect.SpriteEffect
Effect.MeshEffect

Shader.TextureSample
Shader.Multiply
Shader.Lerp
```

---

### GraphLink

```cpp
struct GraphLink {
    GraphId id = 0;
    GraphId fromPinId = 0;
    GraphId toPinId = 0;
};
```

リンクは原則として以下だけ許可してください。

```text
OutputPin → InputPin
```

---

### GraphDocument

```cpp
struct GraphDocument {
    std::string graphType;
    uint32_t version = 1;

    std::vector<GraphNode> nodes;
    std::vector<GraphLink> links;

    GraphId nextId = 1;

    nlohmann::json metadata;
};
```

`graphType` の例:

```text
RenderPath
SceneFlow
TransitionEffect
EffectGraph
ShaderGraph
```

---

## RenderPath Graph の考え方

RenderPath Graph は、`.scene.json` の `Header.passOrder` を視覚的に編集するためのグラフです。

### 基本方針

```text
GraphDocument
  ↓ compile
SceneHeader.passOrder
  ↓ existing runtime
ScenePassExecutor
```

Renderer は GraphDocument を知らなくて構いません。

---

## RenderPath ノード種類

最低限、以下を実装してください。

```text
RenderPath.Clear
RenderPath.DepthPrepass
RenderPath.Compute
RenderPath.Draw
RenderPath.PostProcess
RenderPath.Blit
RenderPath.Raytracing
RenderPath.RenderScene
RenderPath.FullscreenCopy
RenderPath.TemporaryTarget
RenderPath.RenderTarget
RenderPath.View
RenderPath.Comment
RenderPath.Reroute
```

最初に編集対応するのは以下で構いません。

```text
Clear
DepthPrepass
Compute
Draw
PostProcess
Blit
```

Raytracing / RenderScene / FullscreenCopy はまず表示だけでも構いません。

---

## RenderPath ノードのピン設計

### 共通 Flow ピン

基本パスノードには実行順を表す Flow ピンを持たせてください。

```text
入力: In  / Flow
出力: Out / Flow
```

見た目上は、通常の Texture ピンとは区別してください。

---

### ClearNode

```text
Type: RenderPath.Clear
Inputs:
  In: Flow
Outputs:
  Out: Flow
Properties:
  enabled
  target
  color
  depth
  stencil
```

---

### DepthPrepassNode

```text
Type: RenderPath.DepthPrepass
Inputs:
  In: Flow
Outputs:
  Out: Flow
  Depth: DepthTexture
Properties:
  enabled
  queue
  passName
  source / dest if existing ScenePassDesc has them
```

---

### DrawNode

```text
Type: RenderPath.Draw
Inputs:
  In: Flow
Outputs:
  Out: Flow
Properties:
  enabled
  queue
  passName
  source
  dest
```

---

### ComputeNode

```text
Type: RenderPath.Compute
Inputs:
  In: Flow
  SourceColor: Texture2D optional
  SourceDepth: DepthTexture optional
Outputs:
  Out: Flow
  DestColor: Texture2DUAV optional
Properties:
  enabled
  material
  pipeline
  dispatchMode
  source
  dest
  extraSources
```

---

### PostProcessNode

```text
Type: RenderPath.PostProcess
Inputs:
  In: Flow
  SourceColor: Texture2D
  SourceDepth: DepthTexture optional
  ExtraTexture: Texture2D optional / dynamic
Outputs:
  Out: Flow
  DestColor: Texture2DUAV
Properties:
  enabled
  material
  source
  dest
  extraSources
  parameters
```

PostProcess の実行は既存の `PostProcessExecutor` に任せてください。

---

### BlitNode

```text
Type: RenderPath.Blit
Inputs:
  In: Flow
  SourceColor: Texture2D
Outputs:
  Out: Flow
  View: View optional
Properties:
  enabled
  source
  dest
  material optional
```

---

### TemporaryTargetNode

```text
Type: RenderPath.TemporaryTarget
Inputs:
  none or In: Flow if needed
Outputs:
  Texture: Texture2DUAV
Properties:
  name
  format
  widthScale
  heightScale
  createUAV
  persistent
```

最初は `PostProcessDebugTemp`、将来的には `BloomTempA` / `BloomTempB` などを表現できるようにしてください。

---

### ViewNode

```text
Type: RenderPath.View
Inputs:
  Color: Texture2D
Outputs:
  none
Properties:
  name = View
```

RenderPath Validator は、最終的に View に出力されるパスがあるか確認してください。

---

## UI / 見た目の仕様

### Unity 風ノード

ノードは以下の構成で描画してください。

```text
┌──────────────────────────────┐
│ ● Node Title                  │  ← title bar
├──────────────────────────────┤
│ Property A    value           │
│ Property B    value           │
├──────────────────────────────┤
│ ○ Input Pin        Output ●   │
│ ○ Input Pin        Output ●   │
├──────────────────────────────┤
│ ⚠ Validation message          │  ← optional error area
└──────────────────────────────┘
```

### 種類別アクセント

`NodeGraphStyle` でノード種別ごとの色を管理してください。

例:

```text
Clear          gray / blue
DepthPrepass   purple
Draw           green
Compute        cyan
PostProcess    magenta
Blit           orange
Raytracing     red
Temporary      yellow
View           white / gold
```

色指定は `NodeGraphStyle` に閉じ込め、各 Tool や Node が直接 ImGui の色を乱用しないでください。

---

## Editor Tool の要件

### RenderPathGraphTool

配置:

```text
Engine/Editor/Tools/Builtin/RenderPathGraph/RenderPathGraphTool.h
Engine/Editor/Tools/Builtin/RenderPathGraph/RenderPathGraphTool.cpp
```

役割:

```text
- ToolPanel から開ける
- 現在開いている Scene の passOrder をノード表示する
- GraphDocument を保持する
- Import / Validate / Compile / Save の UI を提供する
- 実処理は専用クラスへ委譲する
```

Tool 上部に以下のような操作を用意してください。

```text
[Import From Current Scene]
[Validate]
[Compile Preview]
[Apply To Scene]
[Save Scene]
[Export Graph]
[Import Graph]
[Reset Layout]
```

最初からすべて完全に動かせなくても良いですが、クラス構造として拡張できるようにしてください。

---

## Phase 0: passOrder 読み取り専用ビュー

### 目的

最初に既存 `.scene.json` / `SceneHeader.passOrder` をノードとして表示してください。

この段階では保存しないでください。

### 実装内容

```text
- RenderPathGraphTool を BuiltinEditorTools に登録
- 現在開いている SceneHeader.passOrder を取得
- RenderPathGraphImporter で GraphDocument を生成
- NodeGraphView で表示
- 実行順を Flow link として表示
- ノード移動 / ズーム / 選択ができる
```

### 禁止

```text
- SceneHeader.passOrder を変更しない
- .scene.json を保存しない
- Renderer を変更しない
- ScenePassExecutor を変更しない
```

---

## Phase 1: 汎用ノード基盤

### 実装内容

```text
- GraphDocument
- GraphNode
- GraphPin
- GraphLink
- GraphSerializer
- GraphNodeRegistry
- GraphNodeDefinition
- NodeGraphContext
- NodeGraphView
- NodeGraphStyle
```

### できること

```text
- ノード作成
- ノード削除
- ノード移動
- リンク作成
- リンク削除
- 選択
- 右クリックメニュー
- 基本レイアウト保存
```

### 注意

汎用基盤は RenderPath 専用の include に依存しないでください。

---

## Phase 2: Unity 風 UI 作り込み

### 実装内容

```text
- タイトルバー
- 角丸背景
- 種類別アクセント
- 入力ピン / 出力ピン描画
- Flow pin と Data pin の見た目分離
- エラー表示領域
- Disabled 表示
- Node search popup の土台
- MiniMap / Overview が可能なら追加
```

### 注意

見た目用コードは `NodeGraphStyle` / `NodeGraphDrawUtils` に分離してください。

---

## Phase 3: RenderPath 専用ノード定義

### 実装内容

```text
- RenderPathGraphNodeFactory
- Clear / DepthPrepass / Compute / Draw / PostProcess / Blit / Raytracing / View / TemporaryTarget node definition
- ScenePassType と GraphNode type の対応表
```

対応例:

```text
ScenePassType::Clear        → RenderPath.Clear
ScenePassType::DepthPrepass → RenderPath.DepthPrepass
ScenePassType::Compute      → RenderPath.Compute
ScenePassType::Draw         → RenderPath.Draw
ScenePassType::PostProcess  → RenderPath.PostProcess
ScenePassType::Blit         → RenderPath.Blit
ScenePassType::Raytracing   → RenderPath.Raytracing
ScenePassType::RenderScene  → RenderPath.RenderScene
```

---

## Phase 4: passOrder → GraphDocument Import

### 実装内容

```text
RenderPathGraphImporter
  - SceneHeader.passOrder を読む
  - 各 ScenePassDesc を GraphNode に変換
  - 実行順 Flow link を作成
  - source / dest / material / queue / passName / enabled を properties に入れる
  - 自動レイアウトする
```

### 自動レイアウト

最初は横並びで構いません。

```text
Clear → DepthPrepass → Compute → Draw → PostProcess → Blit → View
```

将来的に縦方向グループ化できるようにしてください。

---

## Phase 5: Validation

### RenderPathGraphValidator

以下を検証してください。

```text
- Flow が Output → Input で接続されている
- Flow に循環がない
- 実行開始ノードがある
- View または View への Blit がある
- 必須入力ピンが接続されている
- ピン型が一致している
- source / dest に存在しない RenderTarget 名を指定していない
- PostProcess の source と dest が同一でない
- Compute / PostProcess の dest が UAV 対応である
- Material が存在する
- Pipeline が存在する
- Draw queue が空でない
- Depth が必要な pass に Depth がある
- disabled node の扱いが明確
```

### Validation 結果

```cpp
struct GraphValidationMessage {
    enum class Severity { Info, Warning, Error };
    Severity severity;
    GraphId nodeId;
    GraphId pinId;
    std::string message;
};
```

エラーはノード下部に表示してください。

エラーがある場合、`Apply To Scene` と `Save Scene` は実行しないでください。

---

## Phase 6: GraphDocument → passOrder Compile

### RenderPathGraphCompiler

```text
GraphDocument
  ↓ validate
Flow link を実行順に辿る
  ↓
ScenePassDesc 配列を生成
  ↓
SceneHeader.passOrder に反映
```

### 要件

```text
- enabled を保持
- type を保持
- source / dest を保持
- material を保持
- queue / passName を保持
- extraSources を保持
- unknown field を可能な限り失わない
```

### 重要

`ScenePassDesc` の既存 JSON 仕様を壊さないでください。

既存 scene json に存在するが UI 未対応の項目は、可能な限り `properties.raw` や `metadata` に保持して、保存時に消さないようにしてください。

---

## Phase 7: 保存 / バックアップ / 再読み込み

### 保存方針

最初から直接上書きしないでください。

推奨:

```text
1. Apply To Scene でメモリ上の SceneHeader に反映
2. Save Scene で .scene.json を保存
3. 保存前に .bak を作成
4. 保存後に再読み込みして Import 結果が一致するか確認
```

または、最初は以下のような preview export でも構いません。

```text
SampleScene.renderpath.preview.json
```

### GraphDocument 保存

RenderPathGraph 自体も保存できるようにしてください。

```text
Engine/Assets/Graphs/RenderPath/<SceneName>.renderpath.graph.json
```

ただし、Runtime がこの graph json を直接読む必要はありません。

---

## Phase 8: PostProcess / TemporaryTarget 対応

### 目的

固定描画パスが安定した後、PostProcess と TemporaryTarget をノードで扱えるようにしてください。

### 実装内容

```text
- PostProcessNode で material asset を表示 / 変更
- Drag & Drop で material を設定
- source / dest の RenderTarget を編集
- extraSources を編集
- TemporaryTargetNode を追加
- PostProcessDebugTemp / BloomTempA / BloomTempB を表現
```

### 注意

PostProcess の実行は既存 `PostProcessExecutor` に任せてください。

ノード側で DirectX12 resource や barrier を直接操作しないでください。

---

## Phase 9: Bloom / Multi Pass 対応

### 目的

Bloom のような複数 pass の PostProcess をノードで扱えるようにしてください。

### 表現方法

2方式を想定してください。

#### 方式 A: 個別 pass ノード

```text
SceneColorFinal
  ↓
BloomPrefilter
  ↓
BlurHorizontal
  ↓
BlurVertical
  ↓
BloomComposite
  ↓
BlitToView
```

#### 方式 B: Stack / Composite ノード

```text
PostProcessStack
  - Grayscale
  - Vignette
  - Bloom
```

初期実装は方式 A で構いません。

将来的に方式 B に拡張できるようにしてください。

---

## Phase 10: 他ツールへの拡張基盤

今回の最終段階では、以下のツールへ展開できる設計になっているか確認してください。

### Scene Flow Editor

```text
Engine/Editor/SceneFlowGraph/
  SceneFlowGraphDocument
  SceneFlowGraphValidator
  SceneFlowGraphCompiler
  SceneFlowGraphTool
```

用途:

```text
- Release 時のシーン遷移順序を編集
- SceneNode
- TransitionNode
- BranchNode
- ConditionNode
- LoadSceneNode
- UnloadSceneNode
```

### Transition Effect Tool

```text
Engine/Editor/TransitionEffectGraph/
```

用途:

```text
- Fade
- Wipe
- PostProcess transition
- SceneColor A / SceneColor B の合成
```

### Effect Graph

```text
Engine/Editor/EffectGraph/
```

用途:

```text
- Particle
- SpriteEffect
- MeshEffect
- Preview
```

### Shader Graph

```text
Engine/Editor/ShaderGraph/
```

用途:

```text
- Material shader generation
- PostProcess shader generation
- Shader reflection / Material parameter 連携
```

今回これらを実装しきる必要はありませんが、`GraphDocument` / `NodeGraphView` / `GraphSerializer` が流用可能であることを確認してください。

---

## Asset Drag & Drop

PostProcessNode では MaterialAsset をドラッグ&ドロップで設定できるようにしてください。

将来的に以下にも対応できるように設計してください。

```text
- MaterialAsset
- PipelineAsset
- ShaderAsset
- SceneAsset
- TransitionEffectAsset
- TextureAsset
- AnimationClipAsset
```

Drag & Drop の具体処理は `RenderPathGraphAssetDragDrop` に分離してください。

---

## Undo / Redo

できれば Editor の既存 Undo システムがあればそれに統合してください。

存在しない場合は、GraphDocument 単位で簡易 Undo/Redo を用意してください。

対象操作:

```text
- Add Node
- Delete Node
- Move Node
- Create Link
- Delete Link
- Change Property
- Apply To Scene
```

ただし、Undo/Redo が大きくなりすぎる場合は後回しで構いません。

後回しにする場合でも、将来追加しやすいように操作を `GraphCommand` に分ける設計にしてください。

---

## JSON 保存形式

### GraphDocument JSON 例

```json
{
  "version": 1,
  "graphType": "RenderPath",
  "metadata": {
    "sceneAssetId": "...",
    "sourceScenePath": "Engine/Assets/Scenes/SampleScene.scene.json"
  },
  "nodes": [
    {
      "id": 1,
      "type": "RenderPath.Clear",
      "displayName": "Clear",
      "position": [100.0, 100.0],
      "enabled": true,
      "properties": {
        "passIndex": 0,
        "raw": {}
      }
    },
    {
      "id": 2,
      "type": "RenderPath.PostProcess",
      "displayName": "PostProcess : Grayscale",
      "position": [500.0, 100.0],
      "enabled": true,
      "properties": {
        "material": "Engine/Assets/Materials/Builtin/PostProcess/Grayscale/grayscale.material.json",
        "source": {
          "colors": ["SceneColorFinal"]
        },
        "dest": {
          "colors": ["PostProcessDebugTempColor"]
        }
      }
    }
  ],
  "links": [
    {
      "id": 100,
      "fromPinId": 11,
      "toPinId": 21
    }
  ]
}
```

---

## .scene.json 反映方針

既存 `.scene.json` の構造に合わせてください。

### 重要

```text
GraphDocument は Editor 保存用
.scene.json は Runtime 実行用
Runtime は .scene.json / SceneHeader.passOrder を読む
Runtime は GraphDocument を読まない
```

---

## ビルド設定

新規 `.cpp` / `.h` を追加した場合は、以下に必ず追加してください。

```text
Engine/NEMEngine.vcxproj
Engine/NEMEngine.vcxproj.filters
```

既存の Debug / Develop / Release すべてでビルドできるようにしてください。

---

## ログとエラー処理

以下の場合はクラッシュさせず、Editor 上とログに出してください。

```text
- current scene がない
- passOrder が空
- 未対応 ScenePassType がある
- GraphDocument の JSON parse に失敗
- link の pin が存在しない
- node type が未登録
- material が見つからない
- render target が見つからない
- source / dest が不正
- Validation error がある状態で Apply / Save しようとした
- scene 保存に失敗
```

ログには、scene 名、node id、node type、pass index が分かるようにしてください。

---

## パフォーマンス方針

Editor Tool とはいえ、重くならないようにしてください。

```text
- 毎フレーム scene json を読み直さない
- 毎フレーム GraphDocument を再生成しない
- Validation は変更時または Validate ボタン時に実行
- AssetDatabase 検索はキャッシュする
- Node style は毎フレーム大量生成しない
- 大規模 scene でも使えるように O(N) を意識する
```

---

## 実装優先順位まとめ

以下の順番で実装してください。

```text
Phase 0:
  RenderPathGraphTool の読み取り専用ビュー

Phase 1:
  汎用 GraphDocument / GraphNode / GraphPin / GraphLink

Phase 2:
  NodeGraphView / NodeGraphStyle / Unity 風 UI

Phase 3:
  RenderPath 専用 NodeDefinition / NodeFactory

Phase 4:
  SceneHeader.passOrder → GraphDocument Import

Phase 5:
  RenderPathGraphValidator

Phase 6:
  GraphDocument → SceneHeader.passOrder Compile

Phase 7:
  Save / Backup / Reimport check

Phase 8:
  PostProcess / TemporaryTarget / Material drag & drop

Phase 9:
  Bloom / Multi Pass PostProcess 表現

Phase 10:
  SceneFlow / TransitionEffect / EffectGraph / ShaderGraph へ拡張できる基盤確認
```

---

## 最小完成条件

最低限、以下ができる状態にしてください。

```text
1. ToolPanel から RenderPath Graph Tool を開ける
2. 現在の SceneHeader.passOrder がノード表示される
3. Unity 風のノード見た目になっている
4. ノード移動、ズーム、選択ができる
5. Flow link で実行順が見える
6. Clear / DepthPrepass / Compute / Draw / PostProcess / Blit が表示できる
7. Validation を実行できる
8. Validation エラーをノードに表示できる
9. Graph から passOrder へ Compile Preview できる
10. エラーがなければ SceneHeader.passOrder に Apply できる
11. 保存前にバックアップを作れる
12. 保存後に再読み込みして graph が破綻しない
13. 既存 Runtime 描画が壊れていない
14. 既存 Editor 表示が壊れていない
15. ScenePassExecutor にノード専用処理が追加されていない
```

---

## 禁止事項

以下は禁止です。

```text
- Runtime 側に imgui-node-editor 依存を入れる
- GraphDocument を Runtime 実行データにする
- NodeGraphTool から ScenePassExecutor を直接呼ぶ
- NodeGraphTool が DirectX12 resource / barrier を直接操作する
- RenderPathGraphTool に全責務を詰め込む
- ScenePassExecutor にノード専用処理を追加する
- Validation なしで .scene.json を上書きする
- 既存 scene json の未知フィールドを不用意に消す
- 既存の Draw / Compute / Blit / Raytracing / PostProcess の挙動を変える
- Project 直下の参考 shader を正式 asset として参照する
- C# スクリプトを使って RenderPath Editor を実装する
```

---

## CodexCLI への作業指示

この実装は大規模です。

一度にすべてを雑に実装せず、以下を守ってください。

```text
- まず既存構造を確認する
- SceneHeader / ScenePassDesc / ScenePassType / BuiltinEditorTools / IEditorTool の既存実装に合わせる
- 新規ファイルは責務ごとに分ける
- 大きすぎる cpp を作らない
- 既存挙動を変える変更は最小限にする
- ビルドが通る単位で段階的に実装する
- 可能なら Phase ごとに commit できる粒度にする
- Debug / Develop / Release の全構成でビルドできるようにする
```

---

## 実装後の確認項目

```text
[ ] NEMEngine.vcxproj に新規ファイルが追加されている
[ ] NEMEngine.vcxproj.filters に新規ファイルが整理されている
[ ] ToolPanel に RenderPath Graph Tool が表示される
[ ] SampleScene.scene.json の passOrder がノード化される
[ ] SponzaScene.scene.json のような大きい scene でも落ちない
[ ] 未対応 pass があっても Unknown node として表示される
[ ] ノードを移動できる
[ ] リンクが表示される
[ ] Validation error が表示される
[ ] Graph から passOrder へ compile preview できる
[ ] Apply 前に Validation が走る
[ ] Save 前に backup が作られる
[ ] 保存後に再読み込みできる
[ ] 既存 Runtime 描画が変わらない
[ ] 既存 Editor viewport が変わらない
[ ] PostProcess が既存通り動作する
[ ] ScenePassExecutor に不要な責務が増えていない
```

---

## 将来拡張メモ

今回の RenderPath Graph Editor 実装後、次の順番で拡張してください。

```text
1. Transition Effect Asset Tool
2. Scene Flow Editor
3. Effect Graph 2D / 3D
4. Shader Graph
```

Scene Flow Editor では、RenderPath Graph と同じ汎用 GraphDocument / NodeGraphView を使い、node type だけを変えてください。

```text
SceneFlow.Scene
SceneFlow.Transition
SceneFlow.Branch
SceneFlow.Condition
SceneFlow.LoadScene
SceneFlow.UnloadScene
```

Transition Effect Tool では、遷移演出アセットを作れるようにしてください。

```text
Transition.Fade
Transition.Wipe
Transition.PostProcess
Transition.CrossFade
```

Effect Graph / Shader Graph は影響範囲が大きいため、RenderPath / Material / PostProcess が安定してから着手してください。
