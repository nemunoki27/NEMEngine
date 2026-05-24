# NEMEngine-09 修正指示書: Raytracing反射結果のViewport反映 + RenderQueue/RenderPhase enum化

## 目的

現在の `NEMEngine-09.zip` では、`DispatchRays` 自体は通っているものの、ViewportPanel の表示に Raytracing Reflection の結果が反映されていない。

また、`Sprite`, `Mesh`, `Text` それぞれに `queue` が存在しているが、現在の描画パスは固定化されているため、文字列ベースのQueue管理ではなく `enum class` によるRenderPhase管理へ移行したい。

この修正では、以下の2点を実装する。

1. `RaytracingReflectionPass` の結果が `ViewportPanel` に正しく表示されるようにする。
2. `std::string` ベースのQueue/RenderPhaseを `enum class RenderPhase` に置き換える。

---

## 現在確認できている主な問題

### 1. RaytracingReflectionPass の結果が PostProcessStackPass で消されている

現在の描画フローは概ね以下のようになっている。

```text
ClearRenderTargetsPass
DepthPrepass
LightCullingPass
OpaqueRenderPass              -> SceneMain に描画
RaytracingReflectionPass       -> SceneFinal に DispatchRays 結果を書き込み
TransparentRenderPass          -> SceneFinal に描画
PostProcessMaskedUiPass        -> SceneFinal に描画
PostProcessStackPass           -> SceneMain を SceneFinal にコピー/上書き
BlitToViewPass                 -> SceneFinal を ViewportPanel へ表示
```

問題は `PostProcessStackPass` が、Raytracing後の `SceneFinal` を使わずに `SceneMain` を入力として扱っている点。

特に、ポストプロセスが無効な場合に以下のような処理で `SceneMain` を `SceneFinal` にコピーしているため、`RaytracingReflectionPass` が `SceneFinal` に書き込んだ反射結果が消える。

```cpp
if (!runtime.HasEnabledPasses()) {
    CopySceneMainToFinal(graphicsCore, sceneMain, sceneFinal);
    return;
}
```

また、ポストプロセスが有効な場合も、最初の入力が `SceneMain` になっているため、Raytracing合成後の `SceneFinal` がポストプロセス入力として使われていない。

```cpp
const char* sourceName = isFirst ? kSceneMainAlias : ...;
const char* destName = isLast ? kSceneColorFinal : ...;
```

このため、`DispatchRays` が成功していても最終的な ViewportPanel には反射結果が表示されない。

---

## 修正方針 1: PostProcessStackPass の入力を SceneFinal に変更する

### 目標の描画フロー

修正後は以下の流れにする。

```text
OpaqueRenderPass
  -> SceneMain

RaytracingReflectionPass
  -> SceneMain を読み、SceneFinal に反射合成結果を書き込む

TransparentRenderPass
  -> SceneFinal に描画

PostProcessMaskedUiPass
  -> SceneFinal に描画

PostProcessStackPass
  -> SceneFinal を入力として使う
  -> Ping/Pong などの一時RTを経由する
  -> 最終結果を SceneFinal に戻す

BlitToViewPass
  -> SceneFinal を ViewportPanel に表示
```

---

## 実装指示 1-1: ポストプロセス無効時に SceneFinal を上書きしない

`PostProcessStackPass` でポストプロセスが無効な場合、すでに `SceneFinal` にはRaytracing結果やTransparent/UI描画結果が入っている前提にする。

そのため、以下のような `SceneMain -> SceneFinal` のコピーは行わない。

### 修正前のイメージ

```cpp
if (!runtime.HasEnabledPasses()) {
    CopySceneMainToFinal(graphicsCore, sceneMain, sceneFinal);
    return;
}
```

### 修正後のイメージ

```cpp
if (!runtime.HasEnabledPasses()) {
    return;
}
```

`CopySceneMainToFinal` が `PostProcessStackPass` 内でしか不要にならない場合は削除してよい。
ただし、他のfallback用途で必要なら残してもよい。

---

## 実装指示 1-2: ポストプロセスの最初の入力を SceneMain ではなく SceneFinal にする

現在は最初のポストプロセス入力が `SceneMain` になっている可能性が高い。
これを `SceneFinal` に変更する。

### 修正前の考え方

```text
SceneMain -> PostProcess -> SceneFinal
```

### 修正後の考え方

```text
SceneFinal -> PostProcess -> SceneFinal
```

ただし、同じテクスチャをSRV入力とUAV/RTV出力として同時に使うのは危険なため、必ず一時RenderTargetを経由すること。

---

## 実装指示 1-3: 1パスだけでも一時RTを使う

現在の実装が、ポストプロセスが2パス以上のときだけPing/Pongを作る設計になっている場合は修正する。

`SceneFinal` を入力にして、最終的にも `SceneFinal` に戻す必要があるため、ポストプロセスが1パスだけでも一時RTが必要。

### 例

#### 1パスの場合

```text
SceneFinal -> PostProcessPing
PostProcessPing -> SceneFinal
```

#### 2パスの場合

```text
SceneFinal -> PostProcessPing
PostProcessPing -> SceneFinal
```

または、既存設計に合わせて以下でもよい。

```text
SceneFinal -> PostProcessPing
PostProcessPing -> SceneFinal
```

#### 3パスの場合

```text
SceneFinal -> PostProcessPing
PostProcessPing -> PostProcessPong
PostProcessPong -> SceneFinal
```

重要なのは、各パスで入力と出力が同一Resourceにならないこと。

---

## 実装指示 1-4: ResourceBarrier / State遷移を正しく行う

`SceneFinal` をポストプロセス入力として読む場合、SRVとして読む前に適切な状態へ遷移させること。

また、最終的に `SceneFinal` へ書き戻す場合は、UAVまたはRenderTargetとして書き込める状態へ遷移させること。

既存の `GraphicsResource` / `RenderTarget` / `RenderGraph` 系のユーティリティがある場合は、それに合わせて実装する。

最低限、以下の状態管理を破綻させないこと。

```text
SceneFinal as input  -> SRV / NonPixelShaderResource / PixelShaderResource
Temporary output     -> UAV or RenderTarget
Final SceneFinal     -> UAV or RenderTarget
BlitToViewPass input -> SRV
```

既存エンジンのResourceState管理方針に従うこと。

---

## 実装指示 1-5: RaytracingReflectionPass のfallbackは維持する

`RaytracingReflectionPass` 側では、以下のような条件でRaytracingが実行できない場合がある。

- DXR非対応
- TLASなし
- ShaderTableなし
- 必要なScene/Material/Instance情報なし
- RaytracingReflectionが無効

この場合は、従来通り `SceneMain -> SceneFinal` のコピーを行うfallbackは残してよい。

重要なのは、`RaytracingReflectionPass` より後の `PostProcessStackPass` が無条件で `SceneFinal` を上書きしないこと。

---

## 修正方針 2: string Queue を enum class RenderPhase に置き換える

現在、RendererComponentやRenderItemで以下のような文字列Queue/Phaseが使われている。

```cpp
std::string queue = "Opaque";
std::string queue = "CanvasPreModel";
std::string renderPhase = "Opaque";
```

また、RenderPass側では以下のような文字列検索が行われている。

```cpp
passBuckets.Find("Opaque");
passBuckets.Find("Transparent");
```

これは固定RenderPathでは危険。

理由:

- typoしてもコンパイルエラーにならない
- 存在しないQueue名でも実行時まで分からない
- `unordered_map<std::string, ...>` により不要なハッシュ計算が発生する
- RenderPathが固定なのに文字列で動的管理しているため設計が曖昧になる
- `ScreenUiPass` 側で未知のPhaseがUI扱いされる可能性がある

そのため、内部表現は `enum class RenderPhase` に変更する。

---

## 実装指示 2-1: RenderPhase enum class を追加する

適切な場所に `RenderPhase` を定義する。

候補:

- `Engine/Render/RenderPhase.h`
- `Engine/Graphics/RenderPhase.h`
- 既存のRenderQueue関連ヘッダ

例:

```cpp
#pragma once

#include <cstdint>
#include <string_view>

namespace Engine {

    enum class RenderPhase : uint8_t {
        Opaque = 0,
        Transparent,
        PostProcessMaskedUI,
        ScreenUI,
        EditorOverlay,
        Count
    };

    constexpr size_t kRenderPhaseCount = static_cast<size_t>(RenderPhase::Count);

    std::string_view ToString(RenderPhase phase);
    RenderPhase RenderPhaseFromString(std::string_view value, RenderPhase fallback = RenderPhase::Opaque);
    bool TryParseRenderPhase(std::string_view value, RenderPhase& outPhase);

}
```

`ToString` と `RenderPhaseFromString` はJSON互換・Editor表示・Debug表示のために用意する。

---

## 実装指示 2-2: 旧名称 CanvasPreModel の互換性を維持する

既存データやJSONに `CanvasPreModel` が保存されている可能性がある。

ただし、現在の描画順では `CanvasPreModel` という名前は実態と合っていない。
`ScreenUiPass` は最終ViewportへのBlit後にUIを重ねる用途に近いため、名称としては `ScreenUI` または `Overlay` が適切。

今回は内部名を `ScreenUI` にする。

`RenderPhaseFromString` では互換性のため、以下を許容する。

```cpp
"CanvasPreModel" -> RenderPhase::ScreenUI
"ScreenUI"       -> RenderPhase::ScreenUI
"Overlay"        -> RenderPhase::ScreenUI  // 必要なら
```

保存時は新しい名称 `ScreenUI` で保存する。

```cpp
ToString(RenderPhase::ScreenUI) -> "ScreenUI"
```

---

## 実装指示 2-3: RendererComponent の queue を RenderPhase に変更する

対象例:

- `MeshRendererComponent`
- `SpriteRendererComponent`
- `TextRendererComponent`
- その他 `queue` を持つRendererComponent

### 修正前

```cpp
std::string queue = "Opaque";
```

### 修正後

```cpp
RenderPhase queue = RenderPhase::Opaque;
```

Sprite/Textのデフォルトは、現在の挙動に合わせて基本的には `ScreenUI` が自然。
ただし、既存挙動でポストプロセス前に描画したいものがある場合は `PostProcessMaskedUI` を使い分ける。

推奨デフォルト:

```cpp
MeshRendererComponent::queue   = RenderPhase::Opaque;
SpriteRendererComponent::queue = RenderPhase::ScreenUI;
TextRendererComponent::queue   = RenderPhase::ScreenUI;
```

既存アセット/Sceneの挙動が大きく変わらないよう、現行のデフォルト文字列に対応したPhaseへ変換すること。

---

## 実装指示 2-4: JSONシリアライズは文字列互換を維持する

C++内部は `RenderPhase` にするが、JSONは可読性と互換性のため文字列のままでよい。

### 読み込み例

```cpp
const std::string queueName = json.value("queue", "Opaque");
component.queue = RenderPhaseFromString(queueName, RenderPhase::Opaque);
```

### 保存例

```cpp
json["queue"] = std::string(ToString(component.queue));
```

注意:

- 不明な文字列の場合はfallbackする
- 可能ならWarningログを出す
- 保存時は必ず新しい正式名称で保存する

---

## 実装指示 2-5: RenderItem の renderPhase を RenderPhase に変更する

対象例:

- `RenderItem`
- `RenderPassItem`
- `IRenderItemExtractor`
- `RenderPassItemCollector`
- `RenderQueue`

### 修正前

```cpp
std::string renderPhase;
```

### 修正後

```cpp
RenderPhase renderPhase = RenderPhase::Opaque;
```

Extractor側でComponentの `queue` をそのままRenderItemへ渡す。

```cpp
item.renderPhase = component.queue;
```

---

## 実装指示 2-6: RenderPassPhaseBuckets を文字列mapからenum配列へ変更する

現在、以下のような実装がある場合は修正する。

```cpp
std::unordered_map<std::string, RenderPassItemList> buckets;
```

固定RenderPathでは、文字列mapよりも `std::array` が適している。

### 推奨実装

```cpp
class RenderPassPhaseBuckets {
public:
    RenderPassItemList& Get(RenderPhase phase) {
        return buckets_[ToIndex(phase)];
    }

    const RenderPassItemList& Get(RenderPhase phase) const {
        return buckets_[ToIndex(phase)];
    }

    void Clear() {
        for (auto& bucket : buckets_) {
            bucket.clear();
        }
    }

private:
    static constexpr size_t ToIndex(RenderPhase phase) {
        return static_cast<size_t>(phase);
    }

    std::array<RenderPassItemList, kRenderPhaseCount> buckets_;
};
```

必要に応じて `Find` 相当の関数を残してもよいが、引数は `RenderPhase` にすること。

```cpp
const RenderPassItemList& Find(RenderPhase phase) const;
```

---

## 実装指示 2-7: 各RenderPassの参照をRenderPhaseへ変更する

以下のような文字列参照をすべて置き換える。

### 修正前

```cpp
passBuckets.Find("Opaque");
passBuckets.Find("Transparent");
```

### 修正後

```cpp
passBuckets.Get(RenderPhase::Opaque);
passBuckets.Get(RenderPhase::Transparent);
```

対象候補:

- `OpaqueRenderPass.cpp`
- `TransparentRenderPass.cpp`
- `DepthPrepass.cpp`
- `ScreenUiPass.cpp`
- `PostProcessMaskedUiPass.cpp`
- その他 `"Opaque"`, `"Transparent"`, `"CanvasPreModel"`, `"ScreenUI"` などを参照している箇所

---

## 実装指示 2-8: ScreenUiPass の未知PhaseをUI扱いしない

現在、`ScreenUiPass` が以下のような判定をしている場合は危険。

```cpp
phase != "Opaque" && phase != "Transparent"
```

このような判定では、未知のQueue名やtypoがScreen UIとして描画されてしまう。

修正後は明示的に `RenderPhase::ScreenUI` のみを描画する。

```cpp
const auto& items = passBuckets.Get(RenderPhase::ScreenUI);
```

`PostProcessMaskedUiPass` も同様に明示的に `RenderPhase::PostProcessMaskedUI` のみ扱う。

---

## 実装指示 2-9: EditorOverlayなど将来用Phaseは必要最小限でよい

`EditorOverlay` は将来的にGizmo/Manipulator/DebugDrawなどを分離するために用意してよい。
ただし、現時点で既存の描画が壊れるなら無理に使わないこと。

重要なのは、少なくとも以下を明確に分けること。

```cpp
Opaque
Transparent
PostProcessMaskedUI
ScreenUI
```

---

## 重点確認ポイント

### Raytracing Reflection

- [ ] `DispatchRays` が実行される
- [ ] `RaytracingReflectionPass` が `SceneFinal` に結果を書き込む
- [ ] `PostProcessStackPass` が `SceneFinal` を `SceneMain` で上書きしない
- [ ] ポストプロセス無効時も `SceneFinal` が維持される
- [ ] ポストプロセス有効時は `SceneFinal` を入力として処理する
- [ ] 1パスだけのポストプロセスでも一時RTを経由する
- [ ] `BlitToViewPass` が最終的な `SceneFinal` をViewportPanelに表示する

### RenderPhase enum化

- [ ] RendererComponentの `queue` が `std::string` ではなく `RenderPhase` になる
- [ ] RenderItemの `renderPhase` が `std::string` ではなく `RenderPhase` になる
- [ ] JSON読み書きでは文字列互換を維持する
- [ ] `CanvasPreModel` は読み込み時に `ScreenUI` へマッピングされる
- [ ] 保存時は `ScreenUI` で保存される
- [ ] `RenderPassPhaseBuckets` が `std::unordered_map<std::string, ...>` ではなく enum index の配列管理になる
- [ ] 各RenderPassが文字列ではなく `RenderPhase::Opaque` などで参照する
- [ ] 未知のPhaseがScreen UI扱いされない

---

## 検索して必ず確認する文字列

実装時、以下の文字列をプロジェクト全体検索して、必要な箇所をすべて修正すること。

```text
"Opaque"
"Transparent"
"CanvasPreModel"
"ScreenUI"
"PostProcessMasked"
"queue"
"renderPhase"
"Find("
"CopySceneMainToFinal"
"kSceneMainAlias"
"kSceneColorFinal"
```

ただし、JSON互換用の `ToString` / `RenderPhaseFromString` 内では文字列を残してよい。

---

## 完了条件

以下を満たしたら完了とする。

1. ビルドが通る。
2. 既存Sceneが読み込める。
3. 既存JSONの `queue` 文字列を読み込める。
4. 保存時には新しい正式名称で保存される。
5. `DispatchRays` 実行後の反射結果がViewportPanelに表示される。
6. ポストプロセス無効時に反射結果が消えない。
7. ポストプロセス有効時にも反射結果を含んだSceneFinalが処理される。
8. Sprite/Text/Meshがそれぞれ適切なRenderPhaseで描画される。
9. 不明なQueue名がScreenUI扱いされない。
10. RenderPhaseの追加・変更時に文字列typoで壊れない設計になっている。

---

## 実装上の注意

- 既存の描画順を不用意に変えないこと。
- `SceneMain` はOpaqueなどのベースカラー用、`SceneFinal` はRaytracing/Transparent/UI/PostProcess後の最終Sceneとして扱うこと。
- `SceneFinal` をSRV入力しながら同時に書き込み先にしないこと。
- 1パスだけでも一時RTを使うこと。
- DXRが使えない環境でもfallbackで表示が壊れないようにすること。
- JSON互換を壊さないこと。
- 旧 `CanvasPreModel` は読み込み互換のみ残し、内部では `ScreenUI` として扱うこと。
- 文字列ベースのPhase分岐を新規に増やさないこと。

---

## 推奨する最終イメージ

```text
SceneMain:
  Opaque描画までのベースシーン

SceneFinal:
  Raytracing反射合成後
  + Transparent
  + PostProcessMaskedUI
  + PostProcessStack結果

ViewportPanel:
  SceneFinalを表示

ScreenUI:
  必要ならViewportへのBlit後に重ねるUI
```

RenderPhaseは以下のように固定パスへ対応させる。

```cpp
enum class RenderPhase : uint8_t {
    Opaque,
    Transparent,
    PostProcessMaskedUI,
    ScreenUI,
    EditorOverlay,
    Count
};
```

以上の方針で、Raytracing ReflectionのViewport反映問題とQueue/RenderPhase設計の整理を同時に行うこと。
