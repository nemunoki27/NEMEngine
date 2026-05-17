# AnimationClipTool v2 実装指示書

対象: `NEMEngine`

このドキュメントは、前回の `AnimationClipTool_ImplementationRequirements.md` を実装した後の追加・修正要件です。  
Codex CLI で既存の作業ツリーを直接編集する前提で書いています。`.zip` を展開して作業する指示ではありません。

Codex に渡す場合は、以下を前提にしてください。

- 既に前回要件の初期実装は入っている。
- 現在の実装は `NEMEngine01.zip` 相当の状態である。
- 既存の `MyGUI` 関数を優先して使う。
- `AnimationCurveTool` は本実装に依存させない。参考にするだけにする。
- MaterialAsset 本体はアニメーション対象にしない。
- 初期対象は C++ Builtin Component のみ。C# Script Component は将来対応。

---

## 0. 現状実装で確認済みの問題

`NEMEngine01.zip` の実装では、以下が不十分または未実装です。v2ではこれらを解消してください。

- キー編集が直感的ではない。
- 2D / 3D で編集可能なプロパティを制限していない。
- UI が見にくく、Unity の Animation Window 的な構成になっていない。
- Save ボタンは実装されているが、上部ツールバーとして明確に配置し、保存失敗理由を見える化する必要がある。
- カーブ表示名が長く、`Transform.localPos.X` のような表示になっている。チャンネル表示は `X/Y/Z/W` または `R/G/B/A` のみにする。
- `Channels` の右側に表示される不要な文字列・冗長な表示を削る。
- キー時点・分割時点の見た目を SceneView 上で確認できない。
- キー追加がしにくい。
- `sin` / `cos` / `Easing` の Bake 機能がない。
- Loop 設定、Loop Bridge、ApplyMode が仕様不足。
- プロパティ選択時に CurveEditor の表示レンジが共有されている。表示レンジは Track 単位で保存する。
- Color / Quaternion が専用 CurveEditor を使っていない。
- `Constant / Linear / Bezier / Spline / Squad` が揃っていない。
- カーブエディター上でキーを動かす時の Snap Step が `0.001f` になっていない。

---

## 1. 作業時の重要ルール

### 1.1 Codex CLI 前提

Codex は CLI でリポジトリの作業ツリー上で起動されている前提です。

- `.zip` を探したり展開したりしない。
- 現在のディレクトリを NEMEngine のルートとして扱う。
- 変更前に `git status` を確認する。
- 既存の未コミット変更がある場合は、破壊的な操作をしない。
- 既存ファイルを全面置換せず、必要箇所を差分修正する。
- 新規 `.cpp` / `.h` を追加した場合、プロジェクトのビルド設定にも追加する。

### 1.2 既存 MyGUI 優先

ツールUIは可能な限り既存の `MyGUI` 関数を使用してください。

特に以下は既存のものを優先します。

- `MyGUI::AssetReferenceField`
- `MyGUI::CurveEditor`
- `MyGUI::CurveEditor` の `CurveFloat / CurveVector3 / CurveColor3 / CurveColor4 / CurveQuaternion` overload
- `MyGUI::Checkbox`
- 既存の Value Edit / Drag / Combo / Asset UI 補助関数

Color と Quaternion は重要です。

- Color Track は汎用 `std::span<CurveChannel>` ではなく、既存の Color 用 `MyGUI::CurveEditor` overload を使う。
- Quaternion Track は汎用 `std::span<CurveChannel>` ではなく、既存の Quaternion 用 `MyGUI::CurveEditor` overload を使う。
- 汎用 float チャンネル CurveEditor に Color / Quaternion を無理やり表示しない。

---

## 2. 2D / 3D モード仕様

### 2.1 判定基準

AnimationClipTool の Target Entity に対し、Entity が持つ Component から 2D / 3D 候補を判定します。

既存コード上の対象 Component:

- 2D 判定
  - `SpriteRendererComponent`
  - `TextRendererComponent`
  - `OrthographicCameraComponent`
- 3D 判定
  - `MeshRendererComponent`
  - `PerspectiveCameraComponent`
- `TransformComponent` のみの場合
  - デフォルトは 3D
- 2D / 3D の両方に該当する場合
  - AnimationClipTool 上でユーザーが `2D / 3D` を切り替えられるようにする。

### 2.2 ツール内モード

以下の enum を追加してください。

```cpp
namespace Engine {
    enum class AnimationClipEditDimension : uint8_t {
        Auto,
        Mode2D,
        Mode3D,
    };
}
```

UIでは以下のように表示します。

```text
Edit Dimension: [Auto / 2D / 3D]
Detected: 2D / 3D / Mixed / Unknown
```

`Auto` の場合:

- 2D のみ該当: 2D
- 3D のみ該当: 3D
- 両方該当: 3D を初期値にする。ただし UI で切り替え可能。
- Transform のみ: 3D

### 2.3 2D 時の Transform 制限

2D モードでは Transform の編集候補を以下に制限します。

表示・AddProperty に出す:

- `Transform.localPos.x`
- `Transform.localPos.y`
- `Transform.localRotation.z`
- `Transform.localScale.x`
- `Transform.localScale.y`

表示・AddProperty に出さない:

- `Transform.localPos.z`
- `Transform.localRotation.x`
- `Transform.localRotation.y`
- Quaternion XYZW の直接編集
- `Transform.localScale.z`

3D モードでは以下を出します。

- `Transform.localPos` as Vector3
- `Transform.localRotation` as Quaternion
- `Transform.localScale` as Vector3

### 2.4 2D 用プロパティの扱い

現在の `AnimationPropertyRegistry` は `Transform.localPos` を Vector3 として扱っています。2D の X/Y だけを出すために、以下のどちらかで実装してください。

推奨:

```text
AnimationPropertyDescriptor に channelMask / editableChannels / displayChannels を追加する。
2D モードでは Vector3 Track を作るが Z channel は表示・編集対象から外す。
```

または、実装都合で以下でも可です。

```text
2D 専用 propertyPath を追加する。
Transform.localPos2D      Vector2
Transform.localRotationZ  Float
Transform.localScale2D    Vector2
```

ただし、JSON 保存時に将来互換を考え、どちらの方式でも `componentName` と `propertyPath` が安定するようにしてください。

---

## 3. AnimationClipAsset / JSON 追加仕様

既存の `AnimationClipAsset` に以下を追加してください。

### 3.1 補間モード

`CurveInterpolationMode` を以下にする。

```cpp
enum class CurveInterpolationMode : uint8_t {
    Constant,
    Linear,
    Bezier,
    Spline,
    Squad,
};
```

既存の `Cubic` は互換用に読み込み時のみ受け付け、内部では `Spline` へ変換してください。

JSON 文字列:

```text
"Constant"
"Linear"
"Bezier"
"Spline"
"Squad"
```

互換読み込み:

```text
"Cubic" -> Spline
```

### 3.2 CurveKey の Tangent

`Bezier` 用に `CurveKey` へ tangent を追加してください。

```cpp
struct CurveKey {
    float time = 0.0f;
    float value = 0.0f;
    CurveInterpolationMode interpolation = CurveInterpolationMode::Linear;

    // Bezier 用。time/value 空間のハンドル。
    Vector2 inTangent = Vector2::AnyInit(0.0f);
    Vector2 outTangent = Vector2::AnyInit(0.0f);
};
```

方針:

- `Constant`: 次キーまで現在値を維持。
- `Linear`: 線形補間。
- `Bezier`: `outTangent` / `inTangent` を使った手動 Bezier。Graph 上でハンドル編集可能にする。
- `Spline`: Catmull-Rom 系の自動補間。ユーザーはハンドルを触らない。
- `Squad`: Quaternion 専用。float / vector / color には出さない。

### 3.3 Track Editor View 保存

表示レンジは Track 単位で保存します。AnimationClip JSON にも保存してください。

```cpp
struct AnimationTrackEditorView {
    float timeMin = 0.0f;
    float timeMax = 1.0f;
    float valueMin = -1.0f;
    float valueMax = 1.0f;
};
```

`AnimationCurveTrack` に追加:

```cpp
AnimationTrackEditorView editorView{};
```

JSON 例:

```json
"editorView": {
  "timeMin": 0.0,
  "timeMax": 1.0,
  "valueMin": -1.0,
  "valueMax": 1.0
}
```

注意:

- Runtime 評価には使わない。
- 次に開いた時に同じ表示範囲になるように保存する。
- Track 選択切り替え時に表示レンジが共有されないようにする。

### 3.4 Auto Duration

`AnimationClipAsset` に保存する設定として `autoDuration` を追加してください。

```cpp
bool autoDuration = false;
```

仕様:

- `autoDuration == true` の場合、全 Track / 全 Channel の最大キー時間を `clip.duration` に反映する。
- キー追加・キー削除・キー移動・キー time 編集後に更新する。
- 最低値は `0.001f`。
- キーが一つもない場合は既存 duration を維持する。

### 3.5 Loop Bridge

Loop 時、`duration` の終了後から 0 秒状態へ自然につなぐ補間区間を持てるようにします。

```cpp
struct AnimationLoopBridgeSettings {
    bool enabled = false;
    float duration = 0.1f;
    CurveInterpolationMode interpolation = CurveInterpolationMode::Linear;
};
```

`AnimationClipAsset` に追加:

```cpp
AnimationLoopBridgeSettings loopBridge{};
```

評価仕様:

- `clip.loop == false`
  - `time < 0` は 0 秒へ clamp。
  - `time > duration` は duration 秒へ clamp。
  - 最終キーが duration より前の場合、duration までは最後の値を維持。
- `clip.loop == true && loopBridge.enabled == false`
  - 通常通り `fmod(time, clip.duration)` で評価。
  - `duration` 丁度の値と 0 秒の値は別として扱う。
- `clip.loop == true && loopBridge.enabled == true`
  - 1周期を `clip.duration + loopBridge.duration` とする。
  - `0.0 ～ clip.duration` は通常 Clip 評価。
  - `clip.duration ～ clip.duration + loopBridge.duration` は、`clip.duration` 時点の値から 0 秒時点の値へ補間する。
  - 自然な見た目を優先する。
  - Bezier が LoopBridge 用に扱いづらい場合、LoopBridge UI 上は `Linear / Spline` のみ対応でも可。

JSON 例:

```json
"loopBridge": {
  "enabled": false,
  "duration": 0.1,
  "interpolation": "Linear"
}
```

### 3.6 Quaternion Multiply Order

Quaternion Track では `Multiply` を許可します。積の順序を Track ごとに選べるようにしてください。

```cpp
enum class QuaternionMultiplyOrder : uint8_t {
    BaseThenCurve, // final = baseRotation * curveRotation
    CurveThenBase, // final = curveRotation * baseRotation
};
```

`AnimationCurveTrack` に追加:

```cpp
QuaternionMultiplyOrder quaternionMultiplyOrder = QuaternionMultiplyOrder::BaseThenCurve;
```

仕様:

- Quaternion の `Override` は `final = curveRotation`。
- Quaternion の `Multiply` は order に従う。
- Quaternion の `Add` は UI では基本的に出さない。内部データに存在した場合は無視または Override 扱いにしてよい。
- Quaternion Track の ApplyMode UI は `Override / Multiply` のみ。

---

## 4. UI レイアウト仕様

Unity の Animation Window 寄りにします。

### 4.1 上部 Toolbar

ウィンドウ上部に以下を並べます。

```text
[Clip Asset Field] [Save] [Revert] [Dirty *]
[Target Entity Field]
[Edit Dimension: Auto/2D/3D] [Detected: ...]
[Play/Pause] [Stop] [Time] [Duration] [Auto Duration] [Loop]
[Loop Bridge] [Bridge Duration] [Bridge Interpolation]
[Pose Preview] [Divisions]
```

Save:

- Clip Asset が設定されている時のみ有効。
- 未設定時は disabled。
- Tooltip に `Set ClipData first.` を出す。
- 保存成功時は `Clip saved.` を表示。
- 保存失敗時は以下を UI に表示する。
  - `clipAssetID`
  - `ResolveFullPath` 結果
  - 失敗理由
  - 例外がある場合は例外内容

Revert:

- Clip Asset から再読み込み。
- Revert 前に Preview 適用中の値を復元する。

Duration:

- `DragFloat` / `InputFloat` の step は通常 UI 用でよい。
- `autoDuration` ON のときは直接編集不可または編集しても次更新で上書きされることを明示する。

### 4.2 メインレイアウト

`ImGui::BeginTable` などで左右分割してください。

```text
Left: Property Tree / Channel rows
Right: Curve Editor + Key Inspector / Generator Panel
```

推奨幅:

- Left: 280～360 px
- Right: 残り全部

### 4.3 Property Tree

Property Tree の階層例:

```text
Transform
  localPos
    X  [+]
    Y  [+]
    Z  [+]       // 2Dでは非表示
  localRotation
    Quaternion   // 3Dのみ
    Z  [+]       // 2Dのみ
  localScale
    X  [+]
    Y  [+]
    Z  [+]       // 2Dでは非表示
SpriteRenderer
  color
    R  [+]
    G  [+]
    B  [+]
    A  [+]
  size
    X  [+]
    Y  [+]
  pivot
    X  [+]
    Y  [+]
```

仕様:

- Channel 行の `+` で、そのチャンネルだけ現在 Time にキーを追加する。
- Track 行または Channel 行の右クリックメニューに `Add Key` を出す。
- Add Key の値は `Curve Evaluate` した値を使う。
- Channel がまだ存在しない場合は、現在 Entity 値または defaultValue を初期値として Track を作る。その上で現在 Time にキーを追加する。
- Missing Property は赤文字で表示し、適用はスキップする。エラーで止めない。
- Track 名は CurveEditor 外のヘッダーに表示する。

### 4.4 チャンネル表示名

CurveEditor 内の Channels 表示は以下だけにしてください。

- Vector: `X`, `Y`, `Z`, `W`
- Color: `R`, `G`, `B`, `A`
- Quaternion: MyGUI の Quaternion 用表示に従う。ただし冗長な `Transform.localRotation.X` のような表示にしない。
- Float: `Value` またはプロパティ名が短い場合のみ `Value`

現状のような以下は禁止です。

```cpp
ref.displayName = BuildTrackLabel(track) + "." + channel.name;
```

代わりに、Track 名はヘッダーで表示し、Channel 名は短くします。

```cpp
ref.displayName = channel.name; // X/Y/Z/R/G/B/A only
```

### 4.5 現在値表示

現在値は冗長に出さず、選択 Track のヘッダーまたは小さなステータス行に出します。

例:

```text
Transform.localPos   X: 1.000  Y: 0.250  Z: 3.000
SpriteRenderer.color R: 1.000  G: 0.500  B: 0.200  A: 1.000
```

---

## 5. キー編集仕様

### 5.1 キー追加

キー追加方法:

- Channel 行の `+` ボタン。
- Track / Channel 右クリックメニューの `Add Key`。

今回の基本仕様では、Curve 上のダブルクリック追加は必須ではありません。余裕があれば追加してください。

キー追加時の値:

```text
現在 Time におけるカーブ Evaluate 値
```

カーブが空の場合:

- 既存 Entity の現在値を defaultValue として使う。
- その値で現在 Time にキーを追加する。

同じ Time に既にキーがある場合:

- 追加せず既存キーを更新する。
- 浮動小数誤差許容は `epsilon = 0.0005f` 程度。

### 5.2 キー選択 Inspector

キー選択時に Inspector を表示します。

編集項目:

- `time`
- `value`
- `interpolation`
- `inTangent`
- `outTangent`

仕様:

- `interpolation` はキーごとに設定する。
- `Bezier` の時だけ tangent 編集欄を有効にする。
- `Spline` の時は tangent 編集不可。
- `Squad` は Quaternion Track のみ選択可能。
- `Constant / Linear / Bezier / Spline` は Float / Vector / Color で選択可能。

### 5.3 Tangent / Bezier ハンドル

Bezier は手動ハンドルありです。

最低実装:

- Key Inspector で `inTangent / outTangent` を数値編集できる。

推奨実装:

- CurveEditor 上でハンドルを表示し、ドラッグ編集できる。

Graph 上ハンドル実装が難しい場合でも、データ構造と評価処理、Inspector 数値編集は必ず実装してください。

### 5.4 Snap Step

カーブエディター上でキーを動かすときの Snap Step は `0.001f` をデフォルトにしてください。

```cpp
CurveEditSetting setting{};
setting.snapStep = 0.001f; // 既存構造に無い場合は追加する
```

既存 `CurveEditSetting` に該当項目が無い場合は追加してください。

対象:

- キー time ドラッグ
- キー value ドラッグ
- Bezier handle ドラッグ

---

## 6. CurveEditor 型別仕様

### 6.1 Track 内部表現

現在の `AnimationCurveTrack` は `std::vector<CurveChannel>` を持っていますが、Color / Quaternion 専用 Editor を使うには型別に扱う必要があります。

Codex は既存構造を壊しすぎない範囲で、以下のどちらかを選んでください。

#### 推奨案 A: Track は channels を維持し、編集時だけ専用 curve に変換しない

`AnimationCurveTrack` の `channels` は維持しつつ、Color / Quaternion の専用 CurveEditor overload が必要とする型へ安全に bridge してください。

ただし、毎フレーム copy-in/copy-out で選択状態やドラッグが壊れないように注意してください。

#### 推奨案 B: Track に型別 Curve を持つ

より大きな改修が可能なら、以下のように型別 curve を持つ設計に寄せてください。

```cpp
using AnimationTrackCurve = std::variant<
    CurveFloat,
    CurveVector3,
    CurveColor3,
    CurveColor4,
    CurveQuaternion
>;
```

ただし既存 JSON 互換を壊さないようにしてください。

### 6.2 Color

Color Track は `MyGUI::CurveEditor(const char*, CurveColor4&, ...)` または `CurveColor3` を使います。

- RGBA をまとめて Color 用 UI として編集できるようにする。
- Channel 表示は `R/G/B/A` のみ。
- `ApplyMode` は `Override / Add / Multiply` を出してよいが、実用上は Override が基本。
- Multiply は component-wise multiply でよい。

### 6.3 Quaternion

Quaternion Track は `MyGUI::CurveEditor(const char*, CurveQuaternion&, ...)` を使います。

仕様:

- Quaternion を汎用 4ch float として表示しない。
- `Squad` は Quaternion Track 専用。
- ApplyMode は `Override / Multiply` のみ。
- Multiply の順序は Track 単位で選択可能。
  - `BaseThenCurve`: `final = baseRotation * curveRotation`
  - `CurveThenBase`: `final = curveRotation * baseRotation`

---

## 7. Preview / Pose Preview 仕様

### 7.1 Target Entity Preview

Preview 再生や Time Scrub 中は、Target Entity 自体へ一時的に値を適用します。

- Preview 開始時に基準値をキャッシュする。
- Stop / Tool Close / Target変更 / Clip変更 / Revert / Save前後必要時には必ず元へ戻す。
- Add / Multiply は Preview 開始時の base 値を使う。
- 毎フレーム現在値へ加算・乗算しない。

### 7.2 Pose Preview

SceneView 上に、分割時刻ごとの見た目を確認するための Editor 専用 Preview Entity を表示します。

名称: `Pose Preview`

設定:

```cpp
bool posePreviewEnabled = true;
int posePreviewDivisions = 4;
```

仕様:

- `posePreviewDivisions` のデフォルトは `4`。
- UIで編集できる。
- 最小値は `1`。
- `duration` を均等分割する。
- 表示数は `posePreviewDivisions + 1`。

例:

```text
duration = 1.0
posePreviewDivisions = 4
表示時刻 = 0.0, 0.25, 0.5, 0.75, 1.0
```

表示条件:

- Target Entity が設定されている。
- Clip に Track がある。
- Pose Preview が ON。

Preview Entity 仕様:

- Target Entity とは別 Entity として扱う。
- Target Entity の見た目を複製する。
- 各分割時刻で Clip を Evaluate した状態を適用する。
- Hierarchy には表示しない。
- Scene 保存対象にしない。
- 通常 Entity Serialize 対象にしない。
- Editor 専用 Entity として扱う。
- Pose Preview OFF、Target変更、Clip変更、Tool終了時に削除または非表示にする。

実装注意:

- 既存 ECS に Editor-only entity / hidden entity の仕組みがある場合はそれを使う。
- 無い場合は、最初の実装では SceneView 描画専用の ghost data として扱い、保存対象にならないようにする。
- 実装が大きすぎる場合、まず `PosePreviewManager` の形で Tool 内部に閉じてよい。

---

## 8. sin / cos / Easing Bake 仕様

Runtime の Track 評価方式として保持するのではなく、カーブキーへ Bake します。

### 8.1 UI

選択中 Channel または Track に対して以下の Generator Panel を出します。

```text
Generate Curve
  Type: Sin / Cos / Easing
  Start Time
  End Time
  Start Value
  End Value
  Amplitude
  Frequency
  Phase
  Sample Count
  Easing Type
  Apply To: Selected Channel / Selected Track Visible Channels
  [Bake]
```

### 8.2 Sin / Cos

Bake 例:

```cpp
float normalized = i / float(sampleCount);
float angle = normalized * frequency * 2.0f * pi + phase;
float wave = type == Sin ? std::sin(angle) : std::cos(angle);
float value = offset + wave * amplitude;
```

`offset` は `StartValue / EndValue` の扱いとUI設計に合わせて自然に決めてください。まずは `StartValue` を offset としてよいです。

### 8.3 Easing

既存の以下を使用してください。

```text
Engine/Core/Utility/Enum/Easing.h
Engine/Core/Utility/Enum/Easing.cpp
EasingType
EasedValue(EasingType, float)
```

仕様:

```cpp
float eased = EasedValue(easingType, normalized);
float value = Lerp(startValue, endValue, eased);
```

### 8.4 Bake の挙動

- Bake 対象時間範囲内の既存キーは、原則として置き換える。
- 置き換え前に確認 UI を出すか、`Replace Existing Keys` checkbox を用意する。
- Sample Count は最低 2。
- Bake 後は `clipDirty_ = true`。
- `autoDuration` ON なら duration を更新する。

---

## 9. Evaluator 仕様

### 9.1 時間評価

`AnimationClipEvaluator` を拡張し、Loop / LoopBridge に対応してください。

関数例:

```cpp
float ResolveClipEvaluationTime(const AnimationClipAsset& clip, float playbackTime);
```

LoopBridge が有効な場合は、単純な float time だけでは足りないため、Bridge 区間かどうかも返す構造にしてよいです。

```cpp
struct AnimationResolvedTime {
    float clipTime = 0.0f;
    bool inLoopBridge = false;
    float bridgeT = 0.0f;
};
```

Bridge 区間では `duration` 時点の値と 0 秒時点の値を評価し、Bridge 補間で混ぜてください。

### 9.2 補間

`CurveChannel::Evaluate` を以下に対応させてください。

- Constant
- Linear
- Bezier
- Spline

`Squad` は `CurveChannel` では扱わない。Quaternion 専用評価で扱う。

### 9.3 Quaternion

Quaternion 専用 Track で以下を扱います。

- Override
- Multiply
- Multiply order
- Squad interpolation

Squad の完全実装が難しい場合:

- データ構造と UI 選択は用意する。
- 評価は一旦 Slerp / normalized lerp に fallback してよい。
- UI またはコメントで TODO を明記する。

ただし、`Squad` が Float / Vector / Color に出ないことは必須です。

---

## 10. Save / Load 仕様

### 10.1 Save ボタン

上部 Toolbar に必ず配置してください。

```text
[Save]
```

有効条件:

- `clipAssetID_` が有効。
- `hasClip_ == true`。

押下時:

- Preview 中なら必要に応じて値を復元してから保存する。
- `clip_.guid` を `clipAssetID_` に同期する。
- `clip_.name` が空なら asset path stem を使う。
- Track channels / editorView / loopBridge / autoDuration を正規化して JSON 保存する。

### 10.2 エラー表示

保存失敗時、単に `Failed to save` だけではなく、UIに詳細を出してください。

```text
Save failed.
AssetID: ...
Path: ...
Reason: ...
```

`AssetDatabase::ResolveFullPath` が空の場合も UI 表示してください。

### 10.3 JSON 互換

既存の `.animClip.json` を読み込めるようにしてください。

- `editorView` が無ければ default を入れる。
- `autoDuration` が無ければ false。
- `loopBridge` が無ければ disabled。
- `Cubic` は `Spline` として読む。
- `inTangent / outTangent` が無ければ zero。
- `quaternionMultiplyOrder` が無ければ `BaseThenCurve`。

---

## 11. 実装対象ファイルの目安

現在すでに存在する主な対象:

```text
Engine/Core/Animation/Clip/AnimationClipAsset.h
Engine/Core/Animation/Clip/AnimationClipAsset.cpp
Engine/Core/Animation/AnimationClipEvaluator.h
Engine/Core/Animation/AnimationClipEvaluator.cpp
Engine/Core/Animation/Curve/AnimationCurve.h
Engine/Core/Animation/Curve/AnimationCurve.cpp
Engine/Core/Animation/Property/AnimationPropertyRegistry.h
Engine/Core/Animation/Property/AnimationPropertyRegistry.cpp
Engine/Core/Animation/Property/BuiltinAnimationProperties.cpp
Engine/Core/Utility/ImGui/MyGUI.h
Engine/Core/Utility/ImGui/MyGUICurve.cpp
Engine/Core/Utility/Enum/Easing.h
Engine/Core/Utility/Enum/Easing.cpp
Engine/Editor/Animation/CurveEditorState.h
Engine/Editor/Animation/CurveEditorState.cpp
Engine/Editor/Tools/AnimationClipTool.h
Engine/Editor/Tools/AnimationClipTool.cpp
```

追加してよいファイル例:

```text
Engine/Editor/Tools/AnimationClip/AnimationClipToolUI.cpp
Engine/Editor/Tools/AnimationClip/AnimationClipPosePreview.cpp
Engine/Editor/Tools/AnimationClip/AnimationClipCurveGenerator.cpp
```

ただしプロジェクトのファイル構成ルールに合わせてください。

---

## 12. 推奨実装順序

### Phase 1: データ構造と JSON 互換

- `CurveInterpolationMode` を `Constant / Linear / Bezier / Spline / Squad` に拡張。
- `CurveKey` に `inTangent / outTangent` を追加。
- `AnimationTrackEditorView` を追加。
- `autoDuration` を追加。
- `loopBridge` を追加。
- `QuaternionMultiplyOrder` を追加。
- Save / Load JSON を更新。
- 既存 JSON の互換読み込みを実装。

### Phase 2: Curve Evaluate

- Constant / Linear / Bezier / Spline を実装。
- Cubic 互換を Spline に寄せる。
- Snap Step `0.001f` を CurveEditor のキー移動に適用。

### Phase 3: UI 再構成

- 上部 Toolbar 化。
- Save / Revert / Dirty 表示。
- 2D / 3D モード UI。
- Property Tree / CurveEditor の左右分割。
- Channel 表示を `X/Y/Z/W` / `R/G/B/A` に簡略化。
- Track ごとの `CurveEditorState` / `editorView` 保存連携。

### Phase 4: Key Editing

- Channel 行 `+` で Add Key。
- 右クリック `Add Key`。
- Key Inspector。
- キーごとの interpolation 編集。
- Bezier tangent 数値編集。
- 可能なら Graph 上 Bezier handle 編集。

### Phase 5: 型別 CurveEditor

- Color Track は Color 用 `MyGUI::CurveEditor`。
- Quaternion Track は Quaternion 用 `MyGUI::CurveEditor`。
- Quaternion ApplyMode `Override / Multiply`。
- Quaternion multiply order UI。
- Squad は Quaternion 専用として UI/保存/評価 fallback まで実装。

### Phase 6: Preview / Pose Preview

- Target Entity Preview の復元を堅牢化。
- Pose Preview を実装。
- `posePreviewDivisions` を UI で編集可能にする。
- Editor-only / Hidden / Non-serialized を保証する。

### Phase 7: Generator

- sin / cos Bake。
- Easing Bake。
- `Easing.h/.cpp` の `EasingType` / `EasedValue` を使う。

### Phase 8: Loop / Auto Duration

- Auto Duration。
- Loop Bridge UI。
- Loop Bridge 評価。

---

## 13. 受け入れ条件

以下を満たしたら完了です。

### Asset / Save

- `.animClip.json` を読み込める。
- 上部 Toolbar に Save ボタンがある。
- Save 押下で JSON が更新される。
- Save 失敗時に原因が UI に表示される。
- 既存 JSON が壊れずに読み込める。

### 2D / 3D

- Sprite/Text/OrthographicCamera を持つ Entity は 2D 候補。
- Mesh/PerspectiveCamera を持つ Entity は 3D 候補。
- Transform のみは 3D。
- Mixed の場合は Tool 上で切り替え可能。
- 2D では Transform Z / Quaternion XY などが AddProperty に出ない。

### UI

- Unity Animation Window に近い左右分割UI。
- Track 名は CurveEditor 外ヘッダーに出る。
- Channel 表示は短い `X/Y/Z/W` または `R/G/B/A`。
- Channels 欄の冗長な右側文字が消えている。
- 現在値は `X:value` 形式で簡潔に表示。

### Key Editing

- Channel `+` で現在 Time にキー追加できる。
- 右クリック Add Key がある。
- キー追加値は Curve Evaluate 値。
- 同一 time のキーは重複追加せず更新する。
- キーごとに interpolation を設定できる。
- Bezier key は tangent を持つ。
- Spline は Catmull-Rom 的に滑らか。
- CurveEditor 上のキー移動 Snap Step は `0.001f`。

### Color / Quaternion

- Color は Color 用 MyGUI CurveEditor を使う。
- Quaternion は Quaternion 用 MyGUI CurveEditor を使う。
- Quaternion は Override / Multiply を選べる。
- Quaternion Multiply は `base * curve` / `curve * base` を選べる。
- Squad は Quaternion 専用。

### Preview

- Play / Pause / Stop / Time Scrub が動く。
- Preview 中は Target Entity に一時適用される。
- Tool Close / Stop / Target変更 / Clip変更で元値に戻る。
- Pose Preview が ON のとき、duration を均等分割した時刻の preview entity が SceneView に出る。
- Pose Preview entity は Hierarchy に出ない。
- Pose Preview entity は Scene 保存対象にならない。
- Divisions のデフォルトは 4、編集可能、最小 1。

### Generator / Loop

- sin / cos をカーブへ Bake できる。
- Easing を `Easing.h/.cpp` を使って Bake できる。
- Auto Duration ON で全キー最大 time が duration になる。
- Loop ON で通常 fmod ループする。
- Loop Bridge ON で duration 後から 0 秒状態へ自然につながる。

---

## 14. 禁止事項

- `AnimationCurveTool` に依存しない。
- MaterialAsset 本体をアニメーション対象にしない。
- C# Script Component 対応を今回の範囲で無理に実装しない。
- Color / Quaternion を汎用 float チャンネル UI のまま放置しない。
- Quaternion Add を安易に実装しない。
- Preview で変更した Target Entity の値をツール終了後に残さない。
- Pose Preview entity を Scene 保存対象にしない。
- `.zip` を展開して作業する前提のコードや手順を書かない。
- 既存未コミット変更を破壊する git 操作をしない。

---

## 15. Codex 作業開始時の推奨プロンプト

Codex CLI には以下のように指示してください。

```text
NEMEngine の現在の作業ツリーは、前回の AnimationClipTool_ImplementationRequirements.md 実装後の状態です。
この AnimationClipTool_v2_CodexRequirements.md を最優先仕様として、既存実装を差分修正してください。
.zip の展開は不要です。現在のリポジトリを直接編集してください。
まず git status を確認し、既存未コミット変更を壊さないでください。
新規 cpp/h を追加した場合はビルド設定にも追加してください。
段階的に実装し、最後にビルドできる状態にしてください。
```
