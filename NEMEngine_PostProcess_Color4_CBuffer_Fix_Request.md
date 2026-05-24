# NEMEngine PostProcessParameters Color4 / float4 書き込み不具合 修正依頼

## 対象

最新コードは `NEMEngine-06.zip` です。  
この依頼では、PostProcess 周りで発生している **Color4 / float4 パラメータの y, z, w 成分が GPU 側 CBuffer に入らない問題**を修正してください。

## 現象

PostProcess 用シェーダーの `PostProcessParameters` CBuffer に `float4` / `Color4` 相当の値がある場合、Editor のツール上では 4 成分を設定しているにもかかわらず、実行時に GPU 側へ渡される値が以下のようになります。

```txt
x / r : 値が入る
y / g : 0
z / b : 0
w / a : 0
```

PIX で確認しても、最初の 1 成分だけが CBuffer に書き込まれており、残り 3 成分が 0 になっています。

## 原因として疑っている箇所

主に以下のファイルを確認してください。

```txt
Engine/Core/Rendering/PostProcess/PostProcessParameterBufferBuilder.cpp
Engine/Editor/Tools/Builtin/PostProcess/PostProcessStackTool.cpp
```

特に `PostProcessParameterBufferBuilder.cpp` の CBuffer 書き込み処理で、`MaterialParameterValue` 側は `Color4` / `Vector4` として 4 成分を持っていても、Reflection 側の `variable.size` / `rows` / `columns` 判定に引っ張られて、書き込み成分数が 1 成分に制限されている可能性があります。

現在の問題は、おそらく以下のような流れです。

```txt
MaterialParameterValue は Color4 / Vector4
↓
ToFloatArray() などでは 4 成分を持っている
↓
ShaderConstantBufferVariable 側の component count 判定が 1 になる
↓
std::min(componentCount, variableComponentCount) により count = 1 になる
↓
x / r だけが CBuffer に書き込まれる
↓
y / g, z / b, w / a は初期値 0 のまま
```

## 修正方針

### 1. CBuffer 書き込みでは MaterialParameterValue の型を優先する

`Color4` / `Vector4` / `Vector3` / `Vector2` の値を持っている場合は、Reflection の `variable.size` が 4 byte 相当に見えても、値側が持っている成分数を書き込めるようにしてください。

ただし、無条件に 4 成分を書き込むと、もし本当に `float` 変数へ `Color4` を誤って入れた場合に隣の変数を壊す可能性があります。  
そのため、以下の制約を入れてください。

```txt
- 書き込む成分数は MaterialParameterValue の型を優先する
- ただし layout 全体のサイズを超えて書かない
- 可能なら次の CBuffer 変数の offset を超えて書かない
- 最大 4 成分まで
```

推奨イメージです。

```cpp
size_t writableBytes = layoutSize - variable.offset;

if (nextVariableOffset > variable.offset) {
    writableBytes = std::min(writableBytes, size_t(nextVariableOffset - variable.offset));
}

const uint32_t maxWritableComponents =
    static_cast<uint32_t>(writableBytes / sizeof(float));

const uint32_t count =
    std::min(valueComponentCount, std::min(maxWritableComponents, 4u));
```

`PostProcessParameterBufferBuilder` 側では、現在の変数だけでなく、次の変数 offset または書き込み可能 byte 数を使って安全に書き込むようにしてください。

### 2. Reflection の最適化後サイズだけで UI / 書き込み型を決めない

`D3D_SHADER_VARIABLE_DESC::Size` は、状況によっては実際に使用されている成分だけのように見える場合があります。  
そのため、`float4 color;` と宣言していても、シェーダー内で `.r` しか使っていないと CPU 側が 1 成分扱いしてしまう可能性があります。

PostProcess ツールでは、Shader 内で現在使っている成分数ではなく、**ユーザーが設定した型、または HLSL で宣言された型**に従って UI と CBuffer アップロードを行ってください。

可能であれば、以下の Reflection 情報を追加してください。

```cpp
struct ShaderConstantBufferVariable
{
    ...
    uint32_t declaredComponentCount = 1;
    uint32_t declaredByteSize = 4;
};
```

そして、DirectX Shader Reflection の型情報から宣言型ベースで設定してください。

対象候補：

```txt
Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h
Engine/Core/Rendering/RHI/DirectX12/Core/D3D12ShaderCompiler.cpp
```

実装イメージ：

```cpp
D3D12_SHADER_TYPE_DESC typeDesc = {};
type->GetDesc(&typeDesc);

if (typeDesc.Class == D3D_SVC_VECTOR) {
    variable.declaredComponentCount = typeDesc.Columns;
} else if (typeDesc.Class == D3D_SVC_SCALAR) {
    variable.declaredComponentCount = 1;
} else if (typeDesc.Class == D3D_SVC_MATRIX_ROWS || typeDesc.Class == D3D_SVC_MATRIX_COLUMNS) {
    variable.declaredComponentCount = typeDesc.Rows * typeDesc.Columns;
}

variable.declaredByteSize = variable.declaredComponentCount * sizeof(float);
```

ただし、既存構造への影響が大きい場合は、まずは `PostProcessParameterBufferBuilder` 側で安全な書き込み可能範囲を計算して、`Color4` / `Vector4` の 4 成分が確実に入るようにしてください。

### 3. PostProcessStackTool 側の UI 判定も修正する

以下のファイルにも同様の component count 判定があるはずです。

```txt
Engine/Editor/Tools/Builtin/PostProcess/PostProcessStackTool.cpp
```

ここでも Reflection の `variable.size` に引っ張られて `float4` が `float` UI として扱われないようにしてください。

期待する UI は以下です。

```txt
float  -> DragFloat
float2 -> DragFloat2 / Vector2 UI
float3 -> DragFloat3 / Vector3 UI
float4 -> Vector4 UI
Color4 / color / tint 系の名前 -> ColorEdit4
```

命名規則は既存仕様通りで構いません。

```txt
変数名に color / Color / tint / Tint が含まれる float4 は Color UI
それ以外の float4 は Vector4 UI
```

### 4. 修正後も Material 本体は書き換えない

今回の修正は、あくまで Scene ごとの PostProcessStack 設定に保存された `parameterOverrides` を正しく GPU 側へ書き込むためのものです。

以下の設計は維持してください。

```txt
MaterialAsset 本体は書き換えない
Scene ごとの PostProcessStackSettings に parameterOverrides を保存する
PostProcessStackPass 実行時に override 値を CBuffer へ反映する
```

## 追加で確認してほしい点

### t1 を無条件に depth 扱いする処理の確認

今回の Color4 バグとは別ですが、PostProcess 周りで以下の古い互換処理が残っている場合は注意してください。

```txt
register(t1) を depth として扱う
```

例えばユーザー Texture として以下のような宣言をした場合、

```hlsl
Texture2D<float4> gNoiseTexture : register(t1);
```

名前が `gNoiseTexture` でも、`t1` というだけで `gSourceDepth` 扱いされると危険です。

今回の修正範囲で大きく触る必要がなければ必須ではありませんが、PostProcessExecutor 側で以下の仕様になっているか確認してください。

```txt
- gSourceDepth という名前の SRV だけを深度として扱う
- register(t1) の depth fallback は、名前が空、または古い Shader 互換が必要な場合に限定する
- 名前付き SRV は textureOverrides -> DefaultWhite fallback の順で解決する
```

## 期待する動作

以下のような PostProcessParameters がある場合、

```hlsl
cbuffer PostProcessParameters : register(b1)
{
    float4 tintColor;
    float intensity;
};
```

Editor ツールで `tintColor = (0.2, 0.4, 0.6, 1.0)` を設定すると、PIX 上でも CBuffer に以下のように入ること。

```txt
tintColor.x / r = 0.2
tintColor.y / g = 0.4
tintColor.z / b = 0.6
tintColor.w / a = 1.0
intensity       = 設定値
```

また、`tintColor.r` だけを Shader 内で使用している場合でも、CPU 側からは 4 成分を CBuffer へ書き込めること。

## テスト項目

最低限、以下を確認してください。

### 1. float4 / Color4 の 4 成分書き込み

```hlsl
cbuffer PostProcessParameters : register(b1)
{
    float4 tintColor;
};
```

Editor で RGBA 全てに異なる値を入れ、PIX で 4 成分すべてが入っていることを確認してください。

### 2. float4 + float の隣接変数

```hlsl
cbuffer PostProcessParameters : register(b1)
{
    float4 tintColor;
    float intensity;
};
```

`tintColor` の書き込みによって `intensity` が壊れないことを確認してください。

### 3. float2 / float3 / float4

```hlsl
cbuffer PostProcessParameters : register(b1)
{
    float2 offset;
    float3 direction;
    float4 color;
};
```

各 UI と CBuffer 書き込みが正しいことを確認してください。

### 4. Color UI 判定

以下の名前は Color UI になることを確認してください。

```txt
color
Color
tint
Tint
```

例：

```hlsl
float4 tintColor;
float4 ColorFilter;
```

### 5. 既存 PostProcess の動作維持

既存の PostProcessStackTool / PostProcessStackPass / PostProcessExecutor が壊れていないことを確認してください。

```txt
- Stack 設定の保存 / 読み込み
- Pass の enabled
- Pass の並び替え
- Texture override
- DefaultWhite fallback
- Compile error 時に Editor が落ちないこと
```

## 完了条件

この修正は、以下を満たしたら完了です。

```txt
- Color4 / Vector4 / float4 の 4 成分が CBuffer に正しく書き込まれる
- Vector2 / Vector3 も正しい成分数で書き込まれる
- Reflection の variable.size が小さく見えても、値側の成分数を不必要に削らない
- ただし隣の CBuffer 変数や layout 範囲を壊さない
- PostProcessStackTool の UI でも float4 / Color4 が正しく表示される
- MaterialAsset 本体は書き換えず、Scene ごとの override 設計を維持する
- Shader compile error / reflection error / invalid parameter があっても Editor が落ちない
- エラーは ConsolePanel / Logger に出る
```
