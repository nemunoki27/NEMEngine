# AnimationClip Tool 実装要件

対象リポジトリ: `NEMEngine`

このドキュメントは、Unityライクな `AnimationClip` アセット編集ツールを NEMEngine に実装するための要件・設計・実装順序をまとめたものです。Codex に実装させる際は、本ドキュメントの仕様を優先してください。

---

## 0. 実装の目的

`AnimationClip` を **再利用可能なアセット** として作成・編集・保存できる Editor Tool を実装する。

このツールは、今後作成予定の AnimationController / Runtime Animation System から使用される `AnimationClip` アセットを作成するためのもの。今回の実装範囲では、主に **Editor上での編集・プレビュー・保存** を行う。

---

## 1. 確定方針

### 1.1 アニメーション対象

- `MaterialAsset` 本体はアニメーション対象にしない。
- アニメーション対象は **Entity に紐づく Component の値のみ** とする。
- `MaterialComponent` は現時点で存在しないため作らない。
- 描画に使用するベース情報は Renderer 系 Component が持っているため、Renderer Component 内の値を対象にする。

### 1.2 初期対応する Component

初期実装では **C++ Builtin Component のみ** 対応する。

C# Script Component の `[SerializeField]` 等は将来対応とし、今回の実装では対象外。

### 1.3 Preview の ApplyMode 基準値

`Add` / `Multiply` の基準値は **再生開始時の Entity の値** とする。

毎フレーム現在値に加算・乗算してはいけない。

```text
Override: final = curveValue
Add:      final = baseValue + curveValue
Multiply: final = baseValue * curveValue
```

### 1.4 Preview 終了時の復元

AnimationClipTool によるプレビュー適用は **Previewモード中だけ** 有効とする。

- ツールを閉じたら、プレビュー開始前の値へ戻す。
- Stop したときも、基本的にはプレビュー開始前の値へ戻す。
- Time Scrub 中も Preview 中扱いとして値を一時適用する。
- 本番適用・再生は将来作成する Controller 側の責務とする。

### 1.5 Rotation 補間

`Transform.localRotation` は対応する。

ただし初期実装では以下までに留める。

- `Linear`
- 既存の `Cubic` 相当

`Squad` 補間は将来、Quaternion 専用 Track を設計して対応する。今回の実装では未対応でよい。

### 1.6 MyGUI 利用方針

ツールUIを作成するときは、可能な限り **既存の `MyGUI` 関数を使用する**。

特に以下は既存関数を優先して使用する。

- `MyGUI::AssetReferenceField`
- `MyGUI::CurveEditor`
- 既存の Value Edit 系関数
- 既存の Drag & Drop / Asset UI 補助関数がある場合はそれを優先

独自 ImGui 実装は、既存 MyGUI で対応できない箇所に限定する。

---

## 2. 既存コード上の前提

### 2.1 AnimationClip Asset 種別

`AssetType::AnimationClip` は既に存在している。

`AssetDatabase::GuessTypeByPath()` では以下を AnimationClip として認識済み。

```text
.animClip.json
.animClip
```

ProjectPanel の新規作成テンプレートも存在しているため、AssetType の追加は不要。

### 2.2 既存の AnimationClipTool

既存ファイル:

```text
Engine/Editor/Tools/AnimationClipTool.h
Engine/Editor/Tools/AnimationClipTool.cpp
```

現状の `AnimationClipTool` は主に以下を持つ。

- Preview RenderTexture 作成
- Preview Entity 描画
- Preview Camera 操作
- Grid 描画
- RenderTexture 上への Entity Drag & Drop

今回の実装では、これを拡張して AnimationClip 編集 UI を追加する。

### 2.3 既存の Curve 機能

既存ファイル:

```text
Engine/Core/Animation/Curve/AnimationCurve.h
Engine/Core/Animation/Curve/AnimationCurve.cpp
Engine/Core/Utility/ImGui/MyGUICurve.cpp
Engine/Editor/Animation/CurveEditorState.h
Engine/Editor/Animation/CurveEditorState.cpp
```

`MyGUI::CurveEditor()` は既に存在する。

`AnimationCurveTool` はカーブ機能確認用であり、今回の本実装では使用しない。必要に応じて参考にするのは可だが、依存させないこと。

---

## 3. 実装対象ファイル構成

以下の新規追加を基本とする。

```text
Engine/Core/Animation/Clip/AnimationClipAsset.h
Engine/Core/Animation/Clip/AnimationClipAsset.cpp

Engine/Core/Animation/Property/AnimationPropertyRegistry.h
Engine/Core/Animation/Property/AnimationPropertyRegistry.cpp
Engine/Core/Animation/Property/BuiltinAnimationProperties.cpp

Engine/Core/Animation/AnimationClipEvaluator.h
Engine/Core/Animation/AnimationClipEvaluator.cpp
```

既存拡張対象:

```text
Engine/Editor/Tools/AnimationClipTool.h
Engine/Editor/Tools/AnimationClipTool.cpp

Engine/Core/Utility/ImGui/MyGUI.h
Engine/Core/Utility/ImGui/MyGUICurve.cpp

Engine/Editor/Animation/CurveEditorState.h
Engine/Editor/Animation/CurveEditorState.cpp
```

必要に応じて CMake / project file へ新規 cpp を追加すること。

---

## 4. AnimationClipAsset 仕様

### 4.1 基本構造

`AnimationClip` は `.animClip.json` として保存する。

```cpp
namespace Engine {

    enum class AnimationValueType : uint8_t {
        Float,
        Vector2,
        Vector3,
        Vector4,
        Color3,
        Color4,
        Quaternion,
    };

    enum class AnimationApplyMode : uint8_t {
        Override,
        Add,
        Multiply,
    };

    struct AnimationPropertyBinding {
        std::string componentName;
        std::string propertyPath;
        AnimationValueType valueType = AnimationValueType::Float;
    };

    struct AnimationCurveTrack {
        AnimationPropertyBinding binding;
        AnimationApplyMode applyMode = AnimationApplyMode::Override;
        bool visible = true;
        std::vector<CurveChannel> channels;
    };

    struct AnimationClipAsset {
        AssetID guid{};
        std::string name;
        float duration = 1.0f;
        bool loop = false;
        std::vector<AnimationCurveTrack> curveTracks;

        // 将来用。初期実装では読み書きのみ、または空配列維持でよい。
        std::vector<AnimationEventTrack> eventTracks;
    };
}
```

`AnimationEventTrack` は今回未実装でよい。コンパイル都合上、空構造体または TODO コメント付きで `eventTracks` を JSON 上の空配列として維持するだけでもよい。

### 4.2 Track 内部表現

Track は `std::vector<CurveChannel>` を持つ。

理由:

- `MyGUI::CurveEditor()` が `CurveChannel` を基準に作られているため。
- 複数プロパティを同一 Time 軸上で表示しやすいため。
- `Vector3` や `Color4` をチャンネル配列として扱えるため。

チャンネル数:

```text
Float      1: Value
Vector2    2: X, Y
Vector3    3: X, Y, Z
Vector4    4: X, Y, Z, W
Color3     3: R, G, B
Color4     4: R, G, B, A
Quaternion 4: X, Y, Z, W
```

`Transform.localRotation` は初期実装では Quaternion の X/Y/Z/W チャンネルとして保存してよい。ただし `Squad` は未対応。

### 4.3 JSON 形式

保存形式例:

```json
{
  "guid": "",
  "name": "MoveAndColor",
  "duration": 1.0,
  "loop": false,
  "curveTracks": [
    {
      "component": "Transform",
      "property": "localPos",
      "type": "Vector3",
      "applyMode": "Override",
      "visible": true,
      "channels": [
        {
          "name": "X",
          "defaultValue": 0.0,
          "keys": [
            { "time": 0.0, "value": 0.0, "interpolation": "Linear" },
            { "time": 1.0, "value": 3.0, "interpolation": "Linear" }
          ]
        },
        {
          "name": "Y",
          "defaultValue": 0.0,
          "keys": []
        },
        {
          "name": "Z",
          "defaultValue": 0.0,
          "keys": []
        }
      ]
    }
  ],
  "eventTracks": []
}
```

### 4.4 JSON 実装要件

以下を実装する。

```cpp
bool LoadAnimationClipAsset(const std::filesystem::path& path, AnimationClipAsset& outClip);
bool SaveAnimationClipAsset(const std::filesystem::path& path, const AnimationClipAsset& clip);

void to_json(nlohmann::json& out, const AnimationClipAsset& clip);
void from_json(const nlohmann::json& in, AnimationClipAsset& clip);
```

また、以下の相互変換を実装する。

```cpp
std::string ToString(AnimationValueType type);
bool TryParseAnimationValueType(std::string_view text, AnimationValueType& out);

std::string ToString(AnimationApplyMode mode);
bool TryParseAnimationApplyMode(std::string_view text, AnimationApplyMode& out);

std::string ToString(CurveInterpolationMode mode);
bool TryParseCurveInterpolationMode(std::string_view text, CurveInterpolationMode& out);
```

読み込み時の注意:

- 不明な enum 文字列は安全に既定値へフォールバックする。
- `duration <= 0` の場合は `1.0f` に補正する。
- `curveTracks` が存在しない場合は空として扱う。
- `eventTracks` が存在しない場合は空として扱う。
- Track のチャンネル数が `type` と一致しない場合は、足りない分を default channel で補完し、多すぎる分は保持または切り詰める。初期実装では切り詰め推奨。

---

## 5. AnimationPropertyRegistry 仕様

### 5.1 目的

Entity が持つ Component のうち、アニメーション可能なプロパティを列挙・取得・適用するための Registry を作る。

`ComponentTypeRegistry` は Component の存在や JSON 変換は扱えるが、アニメーション可能プロパティ一覧は持っていないため、別 Registry として実装する。

### 5.2 基本型

```cpp
namespace Engine {

    using AnimationPropertyValue = std::variant<
        float,
        Vector2,
        Vector3,
        Vector4,
        Color3,
        Color4,
        Quaternion
    >;

    struct AnimationPropertyDescriptor {
        std::string componentName;
        std::string propertyPath;
        std::string displayName;
        AnimationValueType valueType = AnimationValueType::Float;

        bool (*hasComponent)(ECSWorld& world, const Entity& entity) = nullptr;
        bool (*getValue)(ECSWorld& world, const Entity& entity, AnimationPropertyValue& out) = nullptr;
        bool (*setValue)(ECSWorld& world, const Entity& entity, const AnimationPropertyValue& value) = nullptr;
    };

    class AnimationPropertyRegistry {
    public:
        static AnimationPropertyRegistry& GetInstance();

        void Register(const AnimationPropertyDescriptor& desc);

        const AnimationPropertyDescriptor* Find(
            std::string_view componentName,
            std::string_view propertyPath) const;

        std::vector<const AnimationPropertyDescriptor*> GetPropertiesForEntity(
            ECSWorld& world,
            const Entity& entity) const;
    };
}
```

### 5.3 初期対応プロパティ

初期実装で最低限登録する。

#### TransformComponent

既存ファイル:

```text
Engine/Core/ECS/Component/Builtin/TransformComponent.h
```

対象:

```text
Transform.localPos       Vector3
Transform.localRotation  Quaternion
Transform.localScale     Vector3
```

`setValue` 時は `TransformComponent::isDirty = true` にすること。

可能なら `MarkTransformSubtreeDirty(world, entity)` を使う。依存関係や呼び出し都合で難しい場合でも、最低限 `isDirty = true` は必須。

#### SpriteRendererComponent

既存ファイル:

```text
Engine/Core/ECS/Component/Builtin/Render/SpriteRendererComponent.h
```

対象:

```text
SpriteRenderer.size   Vector2
SpriteRenderer.pivot  Vector2
SpriteRenderer.color  Color4
```

#### MeshRendererComponent

既存ファイル:

```text
Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h
```

初期実装で対応する場合は、Entity に紐づく Renderer Component 内の値のみ対象にする。

最低候補:

```text
MeshRenderer.subMeshes[*].color       Color4
MeshRenderer.subMeshes[*].uvPos       Vector2
MeshRenderer.subMeshes[*].uvRotation  Float
MeshRenderer.subMeshes[*].uvScale     Vector2
MeshRenderer.subMeshes[*].localPos    Vector3
MeshRenderer.subMeshes[*].localRotation Vector3
MeshRenderer.subMeshes[*].localScale  Vector3
```

ただし subMesh は動的配列なので、初期実装では MeshRenderer は後回しでもよい。

MeshRenderer を入れる場合の propertyPath は安定IDベースを推奨する。

```text
subMeshes.{stableID}.color
```

index ベースの `subMeshes[0].color` は、メッシュ差し替えや順序変更で壊れやすいため非推奨。

---

## 6. AnimationClipEvaluator 仕様

### 6.1 目的

指定時刻の AnimationClip を評価し、Target Entity の Component に値を適用する。

Editor Preview と将来 Runtime Controller の両方から使える Core 側クラスにする。

### 6.2 基本関数

```cpp
namespace Engine {

    struct AnimationPreviewBaseValue {
        AnimationPropertyBinding binding;
        AnimationPropertyValue value;
    };

    class AnimationClipEvaluator {
    public:
        static bool EvaluateTrack(
            const AnimationCurveTrack& track,
            float time,
            AnimationPropertyValue& outValue);

        static bool ApplyTrack(
            ECSWorld& world,
            const Entity& entity,
            const AnimationCurveTrack& track,
            float time,
            const AnimationPropertyValue* baseValueOrNull);

        static void ApplyClip(
            ECSWorld& world,
            const Entity& entity,
            const AnimationClipAsset& clip,
            float time,
            std::span<const AnimationPreviewBaseValue> baseValues);
    };
}
```

### 6.3 評価仕様

- `track.binding` に対応する `AnimationPropertyDescriptor` がない場合は何もしない。
- Target Entity が該当 Component を持っていない場合は何もしない。
- `CurveChannel::Evaluate(time)` で値を取得する。
- `track.valueType` に応じて `AnimationPropertyValue` を構築する。
- `ApplyMode` は Track 単位。

### 6.4 Add / Multiply 仕様

`Add` / `Multiply` では再生開始時にキャッシュされた baseValue を使う。

baseValue がない場合:

- `Override` として適用してはいけない。
- 安全のため適用をスキップする。

型ごとの演算:

```text
Float:      +, *
Vector2:    component-wise +, component-wise *
Vector3:    component-wise +, component-wise *
Vector4:    component-wise +, component-wise *
Color3:     component-wise +, component-wise *
Color4:     component-wise +, component-wise *
Quaternion: Add / Multiply は初期実装では非推奨。Override のみ対応でもよい。
```

Quaternion の Multiply を実装する場合は Quaternion 積として扱う。ただし初期実装では Quaternion の ApplyMode UI を `Override` 固定にしてもよい。

---

## 7. AnimationClipTool UI 仕様

### 7.1 全体構成

`AnimationClipTool::DrawEditorTool()` は以下の構成にする。

```text
AnimationClip Tool

[Set ClipData]  AnimationClip asset field
[Save]

[Set Target Entity] Entity drag & drop field

[Play] [Stop] [Loop] Time: 0.00 / Duration
[Time Slider / Drag]

Properties
  [Add Property]
  [eye] Transform.localPos      Apply: Override
  [eye] SpriteRenderer.color    Apply: Add
  [eye] MissingComponent.xxx    Apply: Override  <- red text

Curve Editor
  MyGUI::CurveEditor(...)

Preview RenderTexture
```

既存の Preview RenderTexture 機能は維持する。

### 7.2 Tool 状態

`AnimationClipTool.h` に概ね以下を追加する。

```cpp
AssetID clipAssetID_{};
AnimationClipAsset clip_{};
bool hasClip_ = false;
bool clipDirty_ = false;

UUID targetEntityUUID_{};

CurveEditorState curveState_{};

bool previewActive_ = false;
bool previewPlaying_ = false;
float previewTime_ = 0.0f;
float previewSpeed_ = 1.0f;

std::vector<AnimationPreviewBaseValue> previewBaseValues_;
```

### 7.3 Set ClipData

`Set ClipData` には既存の `MyGUI::AssetReferenceField` を使う。

```cpp
MyGUI::AssetReferenceField(
    "Set ClipData",
    clipAssetID_,
    context.toolContext.assetDatabase,
    { AssetType::AnimationClip }
);
```

仕様:

- AnimationClip アセットのみ受け付ける。
- Set ClipData したとき、Target Entity は自動設定しない。
- 既存 Target Entity は保持してよい。
- AssetID 変更時に `.animClip.json` を読み込み、`clip_` に反映する。
- 読み込み失敗時はエラーログを出し、UI上にも表示する。

### 7.4 Save

`Save` ボタンで現在の `clip_` を `clipAssetID_` のパスへ保存する。

仕様:

- `clipAssetID_` が未設定なら Save ボタンは disabled。
- 保存成功時は `clipDirty_ = false`。
- 保存失敗時はログと UI にエラーを出す。
- Save は Target Entity 未設定でも可能。

### 7.5 Set Target Entity

Hierarchy から Entity をドラッグ & ドロップして設定する。

仕様:

- 既存の Hierarchy DragDrop payload を使う。
- `Set Target Entity` 専用の UI 領域を作る。
- Preview RenderTexture に D&D する既存処理とは別に、明示的な Target Field を用意する。
- Target Entity は Clip Asset に保存しない。
- Target Entity が死んでいる場合は UI で未設定扱いにする。

### 7.6 Add Property

`Add Property` ボタン押下で、Target Entity が持つアニメーション可能 Component / Property 一覧を表示する。

仕様:

- Target Entity 未設定の場合は `Add Property` を disabled。
- Popup / Menu 形式で表示する。
- 表示内容は `AnimationPropertyRegistry::GetPropertiesForEntity()` から取得する。
- Component ごとにグルーピングする。
- 既に追加済みの同一 `componentName + propertyPath` は再追加しない。
- Property 選択時に `AnimationCurveTrack` を作成して `clip_.curveTracks` に追加する。

追加時の初期キー:

- 現在の Entity の値を取得し、各チャンネルの `defaultValue` に設定する。
- 0秒に現在値のキーを作成する。
- 必要なら `clip_.duration` 秒にも現在値のキーを作成してよい。
- 初期実装では 0秒キーのみでも可。

### 7.7 Property List

各 Track を一覧表示する。

表示内容:

```text
[eye] Component.property  ApplyMode Combo  [Remove]
```

仕様:

- `track.visible` が true の場合はカーブを表示対象にする。
- `track.visible` が false の場合はカーブを CurveEditor に表示しない。
- 目アイコンは絵文字でなくてもよい。フォント互換を考え、初期実装では `[O]` / `[-]` などでよい。
- Target Entity が該当 Component / Property を持っていない Track は赤文字表示する。
- Missing Track はエラーで止めず、適用時にスキップする。
- その Entity が後から Component / Property を持った場合、通常色に戻り適用可能になる。

### 7.8 ApplyMode UI

Track ごとに選択できる。

```text
Override
Add
Multiply
```

Quaternion Track は初期実装では `Override` 固定でもよい。

その場合、UI上で Add / Multiply を disabled にするか、選択されても適用時にスキップする。推奨は disabled。

### 7.9 Curve Editor

カーブ編集には必ず `MyGUI::CurveEditor` を使う。

要件:

- `AnimationCurveTool` は使用しない。
- AnimationClipTool 内で `MyGUI::CurveEditor` を直接使用する。
- Time は Clip 全体で共通。
- 複数 Track の表示中 Channel を同じ CurveEditor 上に表示する。
- `track.visible == false` の Track は表示しない。
- `CurveEditorState::currentTime` と `previewTime_` は同期する。
- CurveEditor の Time カーソルをドラッグしたら、その時間の値を Target Entity にプレビュー適用する。

### 7.10 複数 Track 表示の実装方針

既存の `MyGUI::CurveEditor` は `std::span<CurveChannel>` を受ける。

複数 Track の Channel はメモリ上で連続していない可能性があるため、以下のいずれかで対応する。

#### 推奨: MyGUI に参照版オーバーロードを追加

```cpp
struct CurveChannelRef {
    CurveChannel* channel = nullptr;
    std::string displayName;
};

static CurveEditResult CurveEditor(
    const char* id,
    std::span<CurveChannelRef> channels,
    CurveEditorState& state,
    const CurveEditSetting& setting = CurveEditSetting{});
```

既存の `std::span<CurveChannel>` 版は残すこと。

#### 暫定: 表示用バッファへコピーして編集後に戻す

これは最小実装として可。ただし Drag 中・選択状態・キー編集の整合性が崩れやすいので、長期的には非推奨。

可能なら参照版オーバーロードを実装すること。

---

## 8. Preview / Playback 仕様

### 8.1 Play

Play 押下時:

1. Target Entity が有効か確認する。
2. `previewActive_ = true` にする。
3. `previewPlaying_ = true` にする。
4. `previewBaseValues_` に全 Track の現在値をキャッシュする。
5. `previewTime_` から再生を開始する。0から開始してもよいが、現在Timeからの再生を推奨。

### 8.2 Stop

Stop 押下時:

1. `previewPlaying_ = false`。
2. `previewActive_ = false`。
3. `previewBaseValues_` を使って Entity の値をプレビュー開始前へ戻す。
4. `previewBaseValues_` をクリアする。

### 8.3 Tool Close

`openWindow_` が false になった場合、必ず Preview 値を復元する。

```cpp
if (!openWindow_) {
    EndPreviewAndRestore(context);
    return;
}
```

Window の `Begin` が false を返した場合も復元漏れしないよう注意する。

### 8.4 Time Scrub

CurveEditor の currentTime をドラッグした場合:

- `previewTime_` と同期する。
- Target Entity が有効なら、その時刻の値を一時適用する。
- Scrub 開始時にまだ `previewActive_ == false` なら base values をキャッシュして `previewActive_ = true` にする。
- Tool Close / Stop で復元する。

### 8.5 Loop

`clip_.loop` を UI から編集可能にする。

再生時:

```cpp
if (previewTime_ > clip_.duration) {
    if (clip_.loop) {
        previewTime_ = std::fmod(previewTime_, clip_.duration);
    } else {
        previewTime_ = clip_.duration;
        previewPlaying_ = false;
    }
}
```

`duration <= 0` は許容しない。

---

## 9. Curve / Interpolation 仕様

### 9.1 初期対応補間

既存の `CurveInterpolationMode` を使う。

```cpp
enum class CurveInterpolationMode : uint8_t {
    Constant,
    Linear,
    Cubic,
};
```

要件上は以下を目標とする。

```text
Linear
Bezier
Spline
Squad for Rotation
```

ただし初期実装では既存の `Linear` / `Cubic` を使う。

`Bezier` / `Spline` / `Squad` は将来拡張として TODO コメントを残す。

### 9.2 Sin / Cos / Easing

初期実装では、Runtime Track として別形式を作らない。

将来実装する場合は、Curve にキーを Bake する機能として入れる。

例:

```text
Generate Curve
  Type: Sin / Cos / EaseInOutCubic / EaseOutBounce / etc
  Start Time
  End Time
  Amplitude
  Offset
  Samples
  [Bake To Keys]
```

今回の実装では必須ではない。設計上、通常の CurveChannel に Bake できるようにしておくこと。

### 9.3 Event Track

将来、特定時間またはキー位置にイベントを置けるようにする。

今回の実装では不要。

JSON の `eventTracks` は空配列を維持できればよい。

---

## 10. Missing Property 仕様

Clip Asset 内に Track が存在しても、現在の Target Entity が該当プロパティを持っていない場合がある。

例:

```text
Clip: SpriteRenderer.color を持つ
Target Entity: SpriteRendererComponent を持っていない
```

この場合:

- エラーで止めない。
- Track を赤文字で表示する。
- CurveEditor での編集は可能にする。
- Preview 適用時は該当 Track をスキップする。
- Entity に後から該当 Component が追加されたら、赤文字を解除して適用可能にする。

---

## 11. Asset 保存仕様

### 11.1 Set ClipData と Save

- `Set ClipData` に AnimationClip Asset を D&D / 選択する。
- 編集内容はその Asset に対して保存する。
- `Set Target Entity` より前に `Set ClipData` できる。
- `Set ClipData` しただけでは Preview Entity / Target Entity は自動設定しない。

### 11.2 Target Entity は保存しない

`AnimationClip` は再利用可能な Asset なので、特定 Entity への参照を保存してはいけない。

保存するのは `componentName` / `propertyPath` / `curve` / `applyMode` など、任意 Entity に適用可能な情報のみ。

---

## 12. 実装フェーズ

### Phase 1: AnimationClipAsset

実装内容:

- `AnimationClipAsset.h/.cpp` 追加
- Data structure 定義
- JSON load/save
- enum string conversion
- CurveChannel / CurveKey JSON 変換

受け入れ条件:

- `.animClip.json` を読み込める。
- 空の `curveTracks` でも落ちない。
- 保存後に再読込して同じ情報が復元される。

### Phase 2: AnimationPropertyRegistry

実装内容:

- `AnimationPropertyRegistry.h/.cpp` 追加
- `BuiltinAnimationProperties.cpp` 追加
- Transform / SpriteRenderer の初期プロパティを登録

受け入れ条件:

- Target Entity が Transform を持つと `Transform.localPos/localRotation/localScale` が列挙される。
- Target Entity が SpriteRenderer を持つと `SpriteRenderer.size/pivot/color` が列挙される。
- `setValue` で実際の Component 値が変わる。
- Transform の `setValue` では dirty が立つ。

### Phase 3: AnimationClipTool の Asset UI

実装内容:

- `Set ClipData`
- `Save`
- `Set Target Entity`
- `duration` / `loop` UI

受け入れ条件:

- AnimationClip Asset を指定できる。
- Save で `.animClip.json` が更新される。
- Hierarchy から Target Entity を D&D できる。
- Clip と Target は独立している。

### Phase 4: AddProperty / Property List

実装内容:

- AddProperty Popup
- Track 作成
- Track 一覧表示
- Visible toggle
- ApplyMode combo
- Missing Property 赤文字表示
- Remove Track

受け入れ条件:

- Target Entity の持つプロパティだけ AddProperty に出る。
- Track 追加後、Property List に出る。
- 同一 Track は重複追加されない。
- Missing Track が赤文字になる。

### Phase 5: CurveEditor 統合

実装内容:

- 表示中 Track の Channel を `MyGUI::CurveEditor` に渡す。
- 複数 Track を同じ Time 軸上に表示する。
- Time cursor と `previewTime_` を同期する。
- Track visible toggle を反映する。

受け入れ条件:

- `Transform.localPos` と `SpriteRenderer.color` を同時に CurveEditor に表示できる。
- 目ボタンで個別 Track のカーブ表示を消せる。
- CurveEditor 上でキーを編集すると Save 可能な Clip データに反映される。

### Phase 6: Preview / Playback

実装内容:

- Play
- Stop
- Time Scrub
- Loop
- Preview base values cache
- Stop / Tool Close 時の復元

受け入れ条件:

- Play で Target Entity の値が時間に応じて変わる。
- Stop で Preview 開始前の値に戻る。
- Tool を閉じても Preview 値が残らない。
- Time cursor ドラッグで手動確認できる。
- Add / Multiply は再生開始時の値を基準に適用される。

---

## 13. 実装上の注意・禁止事項

### 13.1 禁止事項

- `MaterialAsset` 本体をアニメーション対象にしない。
- Target Entity の UUID を AnimationClip Asset に保存しない。
- `AnimationCurveTool` に依存しない。
- Add / Multiply で毎フレーム現在値へ累積加算・累積乗算しない。
- Missing Property をエラー扱いでツール停止しない。
- Preview 適用値を Tool Close 後に残さない。

### 13.2 推奨事項

- UI は既存 `MyGUI` 関数を優先する。
- Core 側と Editor 側を分離する。
- `AnimationClipEvaluator` は将来 Runtime でも使えるように Editor 依存を入れない。
- `AnimationPropertyRegistry` は C# 対応を将来追加しやすい形にする。
- JSON は後方互換を意識して、不明フィールドを無視する。

---

## 14. 将来拡張 TODO

今回実装しないが、設計上考慮しておく。

### 14.1 C# Script Field Animation

将来的に C# Script Component の SerializeField をアニメーション対象にする。

想定 propertyPath:

```text
Script.{ScriptTypeName}.{FieldName}
```

または Component / Script instance を識別できる既存設計に合わせる。

### 14.2 Quaternion Squad

`Transform.localRotation` の本格的な Quaternion 補間として Squad を追加する。

この場合、4本の float channel ではなく、Quaternion 専用 Key Track を作る可能性がある。

### 14.3 Curve Generator

Sin / Cos / Easing Cheat Sheet 系の動きを Curve に Bake する機能を追加する。

### 14.4 Event Track

AnimationClip の特定時間にイベントを置けるようにする。

---

## 15. 最終受け入れ条件

以下を満たせば、今回の AnimationClip Tool 実装は完了とする。

- `.animClip.json` を Asset として選択できる。
- `Set ClipData` で Clip を読み込める。
- `Save` で Clip を保存できる。
- `Set Target Entity` に Hierarchy から Entity を D&D できる。
- Target Entity が持つ Builtin Component のプロパティを AddProperty できる。
- 少なくとも以下をアニメーション可能:
  - `Transform.localPos`
  - `Transform.localRotation`
  - `Transform.localScale`
  - `SpriteRenderer.size`
  - `SpriteRenderer.pivot`
  - `SpriteRenderer.color`
- 複数プロパティのカーブを同一 CurveEditor 上に表示できる。
- Property 左の表示切替でカーブ表示をON/OFFできる。
- Play / Stop / Time Scrub で Preview できる。
- Preview 終了または Tool Close で Entity の値が元に戻る。
- `Override` / `Add` / `Multiply` を Track ごとに選択できる。
- Add / Multiply は再生開始時の値を基準にする。
- Missing Property は赤文字表示され、ツールは停止しない。
- `AnimationCurveTool` に依存していない。
- UI は可能な限り既存 `MyGUI` 関数を使用している。
