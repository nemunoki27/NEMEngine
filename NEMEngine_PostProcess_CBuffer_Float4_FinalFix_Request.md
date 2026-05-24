# NEMEngine PostProcessParameters float4 / Color4 CBuffer 転送不具合 最終修正依頼

## 目的

`PostProcessParameters` の中に `float4` / `Color4` 相当のパラメータがある場合、現在 PIX 上で先頭成分 `x / r` にしか値が入らず、`y / g`, `z / b`, `w / a` が `0` になっています。

例:

```hlsl
cbuffer PostProcessParameters : register(b1)
{
    float intensity;
    float radius;
    float softness;
    float padding;
    float4 vignetteColor;
};
```

JSON / Editor UI 側では `vignetteColor` に 4 成分が設定されているにもかかわらず、PIX では以下のようになります。

```txt
vignetteColor = { 0.161, 0, 0, 0 }
```

この修正では、`PostProcessParameters` 用 CBuffer 生成時に `float2 / float3 / float4 / Color4 / Vector4` が正しく全成分 GPU へ転送されるようにしてください。

---

## 最重要修正対象

主に以下を修正してください。

```txt
Engine/Core/Rendering/PostProcess/PostProcessParameterBufferBuilder.cpp
```

必要に応じて以下も確認・修正してください。

```txt
Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h
Engine/Core/Rendering/RHI/DirectX12/Core/D3D12ShaderCompiler.cpp
Engine/Editor/Tools/Builtin/PostProcess/PostProcessStackTool.cpp
Engine/Core/Rendering/PostProcess/PostProcessStackService.cpp
Engine/Core/Rendering/PostProcess/PostProcessStackRuntime.cpp
```

---

## 現在疑っている原因

### 原因候補 1: 実行時の `MaterialParameterValue` がまだ `float` 扱いになっている

`PostProcessParameterBufferBuilder` に渡ってくる `parameterOverrides["vignetteColor"]` が、Editor / JSON 上では 4 成分に見えていても、実行時には `float` として保持されている可能性があります。

その場合、Builder 側でどれだけ `Color4` 対応を書いていても、最終的には 1 成分しか書き込まれません。

確認用に、`PostProcessParameterBufferBuilder::Build()` 付近で一時的に以下のようなログを出してください。

```cpp
std::visit([&](const auto& v) {
    using T = std::decay_t<decltype(v)>;

    if constexpr (std::is_same_v<T, Engine::Color4>) {
        Engine::Logger::Output(
            Engine::LogType::Engine,
            std::format(
                "[PP Param Debug] {} = Color4({}, {}, {}, {})",
                variable.name, v.r, v.g, v.b, v.a));
    } else if constexpr (std::is_same_v<T, Engine::Vector4>) {
        Engine::Logger::Output(
            Engine::LogType::Engine,
            std::format(
                "[PP Param Debug] {} = Vector4({}, {}, {}, {})",
                variable.name, v.x, v.y, v.z, v.w));
    } else if constexpr (std::is_same_v<T, float>) {
        Engine::Logger::Output(
            Engine::LogType::Engine,
            std::format(
                "[PP Param Debug] {} = float({})",
                variable.name, v));
    }
}, found->second.value);
```

このログで `vignetteColor = float(0.161)` のように出る場合は、Stack 設定から Runtime へ渡る途中で型が壊れています。

ただし、今回の修正ではその場合でも Reflection 情報から `float4` と判断できるなら、Builder 側で 4 成分として扱えるようにしてください。

---

### 原因候補 2: `nextVariableOffset` によって `float4` の書き込み可能サイズが 1 成分に潰れている

現在の `PostProcessParameterBufferBuilder.cpp` に、次のような思想の処理がある場合は注意してください。

```cpp
if (nextVariableOffset > variable.offset) {
    writableBytes = std::min(writableBytes, nextVariableOffset - variable.offset);
}
```

この処理は安全そうに見えますが、Reflection 結果やソート順の都合で、`float4` の途中が「次の offset」と誤認されると、書き込み可能サイズが 4 bytes になってしまいます。

その結果、`float4 vignetteColor` であっても 1 成分しか書かれません。

今回の不具合ではこの可能性が高いです。

---

## 必須修正方針

### 1. `nextVariableOffset` で成分数を制限しない

`float4` の書き込み成分数を `nextVariableOffset` ベースで決めないでください。

CBuffer 変数の書き込みサイズは、基本的に Reflection から得た宣言型情報を優先してください。

使うべき情報の優先順位は以下です。

```txt
1. variable.declaredComponentCount
2. variable.declaredByteSize
3. variable.size
4. layoutSizeInBytes - variable.offset
```

`float4` と宣言されているなら、Shader 内で `.r` しか使っていなくても CPU 側からは 4 成分を書けるようにしてください。

---

### 2. `declaredComponentCount / declaredByteSize` を使って書き込み成分数を決める

`PostProcessParameterBufferBuilder.cpp` に以下のような方針の処理を入れてください。

```cpp
template<typename TValue>
uint32_t GetMaxWritableComponentCount(
    const Engine::ShaderConstantBufferVariable& variable,
    uint32_t layoutSizeInBytes) {

    if (variable.offset >= layoutSizeInBytes) {
        return 0;
    }

    const uint32_t remainingBytes = layoutSizeInBytes - variable.offset;

    uint32_t declaredBytes = variable.declaredByteSize;
    if (declaredBytes == 0) {
        declaredBytes = variable.size;
    }
    if (declaredBytes == 0) {
        declaredBytes = sizeof(TValue);
    }

    const uint32_t writableBytes = std::min(remainingBytes, declaredBytes);
    return std::min<uint32_t>(writableBytes / sizeof(TValue), 4u);
}
```

そして `WriteScalarArray()` では以下のようにしてください。

```cpp
const uint32_t writableComponentCount =
    GetMaxWritableComponentCount<TValue>(variable, layoutSizeInBytes);

const uint32_t declaredComponentCount =
    std::clamp<uint32_t>(variable.declaredComponentCount, 1u, 4u);

const uint32_t count = std::min({
    componentCount,
    writableComponentCount,
    declaredComponentCount,
    4u
});
```

重要:

```txt
nextVariableOffset を count 計算に使わないでください。
```

---

### 3. Builder 内で Reflection 型に合わせて `MaterialParameterValue` を正規化する

`parameterOverrides` 側が古い JSON や古い Runtime 値の影響で `float` になっていても、Reflection が `float4` と判断しているなら、Builder 側で 4 成分値として扱ってください。

例:

```txt
Reflection: float4 vignetteColor
Runtime value: float(0.161)
```

この場合でも、最低限以下のように展開してください。

```txt
vignetteColor = { 0.161, 0.0, 0.0, 0.0 }
```

ただし、Editor / JSON から 4 成分が取れている場合は必ずその 4 成分を使用してください。

推奨実装イメージ:

```cpp
Engine::MaterialParameterValue NormalizeParameterValueForVariable(
    const Engine::ShaderConstantBufferVariable& variable,
    const Engine::MaterialParameterValue& src) {

    // float 系以外は既存処理を維持
    if (variable.valueType != D3D_SVT_FLOAT) {
        return src;
    }

    const uint32_t componentCount = std::clamp<uint32_t>(
        variable.declaredComponentCount,
        1u,
        4u);

    if (componentCount <= 1) {
        return src;
    }

    const float x = ExtractFloatComponent(src, 0);
    const float y = ExtractFloatComponent(src, 1);
    const float z = ExtractFloatComponent(src, 2);
    const float w = ExtractFloatComponent(src, 3);

    Engine::MaterialParameterValue out;

    if (componentCount == 2) {
        out.value = Engine::Vector2{x, y};
    } else if (componentCount == 3) {
        out.value = Engine::Vector3{x, y, z};
    } else {
        if (IsColorParameterName(variable.name)) {
            out.value = Engine::Color4{x, y, z, w};
        } else {
            out.value = Engine::Vector4{x, y, z, w};
        }
    }

    return out;
}
```

`ExtractFloatComponent()` は、`float`, `Vector2`, `Vector3`, `Vector4`, `Color4` などから指定成分を取り出す helper として実装してください。

- 成分が存在しない場合は `0.0f` を返す
- ただし `Color4` の alpha だけデフォルトを `1.0f` にしたい場合は、既存仕様と合わせてください
- 既存挙動を壊さないことを優先するなら、存在しない成分はすべて `0.0f` で構いません

---

### 4. `Color4 / Vector4` が来た場合は必ず4成分書く

`MaterialParameterValue` が `Color4` または `Vector4` の場合、Reflection の宣言型が `float4` なら必ず 4 成分を書き込んでください。

期待される書き込み:

```txt
Color4{r,g,b,a}
↓
offset + 0  = r
offset + 4  = g
offset + 8  = b
offset + 12 = a
```

PIX 上では以下のように見える必要があります。

```txt
vignetteColor
  C0 = r
  C1 = g
  C2 = b
  C3 = a
```

---

## Reflection 側の確認

以下の構造体に `declaredComponentCount` と `declaredByteSize` があるか確認してください。

```txt
Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h
```

期待:

```cpp
struct ShaderConstantBufferVariable {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t rows = 0;
    uint32_t columns = 0;

    uint32_t declaredComponentCount = 1;
    uint32_t declaredByteSize = 4;

    // その他既存フィールド
};
```

`D3D12ShaderCompiler.cpp` / DXC Reflection 側で、HLSL 宣言型から以下のように値が入るようにしてください。

```txt
float  -> declaredComponentCount = 1, declaredByteSize = 4
float2 -> declaredComponentCount = 2, declaredByteSize = 8
float3 -> declaredComponentCount = 3, declaredByteSize = 12
float4 -> declaredComponentCount = 4, declaredByteSize = 16
```

注意:

```txt
D3D_SHADER_VARIABLE_DESC::Size だけに依存しないでください。
```

Shader 内で一部成分しか使っていない場合に、Reflection のサイズが最適化後の使用状況に引っ張られる可能性があるためです。

---

## UI 側の確認

以下も確認してください。

```txt
Engine/Editor/Tools/Builtin/PostProcess/PostProcessStackTool.cpp
```

`float4` の UI 判定に `variable.size` だけを使わず、`variable.declaredComponentCount` を優先してください。

期待:

```txt
float  -> DragFloat
float2 -> DragVector2
float3 -> DragVector3
float4 -> Vector4 UI or Color UI
```

Color UI 判定は既存の命名規則を維持してください。

例:

```txt
name に color / Color / tint / Tint が含まれる float4 は Color4 UI
それ以外の float4 は Vector4 UI
```

---

## 検証項目

以下の Shader で検証してください。

```hlsl
cbuffer PostProcessParameters : register(b1)
{
    float intensity;
    float radius;
    float softness;
    float padding;
    float4 vignetteColor;
};
```

Editor で以下のような値を設定します。

```txt
intensity     = 0.45
radius        = 0.75
softness      = 0.35
padding       = 0.0
vignetteColor = { 0.161, 0.25, 0.5, 1.0 }
```

PIX で CBV 1 `PostProcessParameters` を見たとき、以下になっていることを確認してください。

```txt
intensity = 0.45
radius = 0.75
softness = 0.35
padding = 0
vignetteColor.C0 = 0.161
vignetteColor.C1 = 0.25
vignetteColor.C2 = 0.5
vignetteColor.C3 = 1.0
```

`vignetteColor.C1/C2/C3` が `0` のままなら未修正です。

---

## 追加のデバッグログ

修正中だけで良いので、以下が分かるログを入れてください。

```txt
[PP Param Debug]
- variable.name
- variable.offset
- variable.size
- variable.declaredComponentCount
- variable.declaredByteSize
- MaterialParameterValue の実際の variant 型
- 書き込んだ成分数
- 書き込んだ値
```

例:

```txt
[PP Param Debug] name=vignetteColor offset=16 size=16 declaredComponents=4 declaredBytes=16 valueType=Color4 writeCount=4 values=(0.161, 0.25, 0.5, 1.0)
```

このログは原因確認後、常時出力ではなく Debug ビルド限定、または必要時のみ有効にできる形にしてください。

---

## 完了条件

この修正の完了条件は以下です。

```txt
- PostProcessParameters の float4 / Color4 が PIX 上で4成分すべて転送される
- float2 / float3 / float4 の UI 表示が declaredComponentCount に基づいて正しく出る
- parameterOverrides が古い scalar 型でも、Reflection が float4 なら Builder 側で破綻しない
- nextVariableOffset によって float4 が 1 成分へ潰れない
- 既存の float / int / bool パラメータ転送を壊さない
- Editor が落ちない
```

---

## 注意事項

今回の修正では、PostProcess の大きな設計変更はしないでください。

目的はあくまで以下です。

```txt
PostProcessParameters の float2 / float3 / float4 / Color4 の CBuffer 転送を正しくすること
```

Renderer 全体のリファクタリング、Asset 形式の大幅変更、PostProcessStackTool の大規模改修は不要です。
