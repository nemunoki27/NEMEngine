#include "ShaderGraphPBRBuilder.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphImportUtility.h"

// c++
#include <algorithm>

using namespace Engine;
using namespace Engine::ShaderGraphImportUtility;
using Kind = ShaderGraphNodeKind;
using Type = ShaderGraphValueType;

//============================================================================
//	ShaderGraphPBRBuilder classMethods
//============================================================================

// ノード化できる標準PBRの値を取り込む
ShaderGraphAsset ShaderGraphPBRBuilder::Build(
	const MaterialAsset& material, const MaterialAsset& defaults, const PipelineStaticSamplerSettings& sampler) {

	ShaderGraphPBRBuilder builder;
	// 標準値へMaterialの編集値を重ねる
	builder.values_ = defaults.parameters;
	builder.values_.MergeFrom(material.parameters);
	const auto displacement = builder.Value("displacementScale", {.value = 0.0f});
	Require(
		std::get<float>(ConvertValue(displacement, Type::Float).value) == 0.0f, "Displacementが有効なMaterialは取り込めません");
	builder.Value("displacementMidpoint", {.value = 0.5f});
	builder.Value("displacementTexture", {.value = AssetID{}});
	// 共通サンプラーと表面出力を作成
	builder.sampler_ = builder.Node(Kind::SamplerState);
	builder.graph_.nodes.back().sampler = sampler;
	const auto output = builder.Node(Kind::SurfaceOutput);
	builder.graph_.nodes.back().position = Vector2(2200.0f, 600.0f);
	builder.graph_.outputNode = output.node;
	builder.Begin("ベースカラー");
	const auto color = builder.Parameter("color", Type::Color, {.value = Color4(1, 1, 1, 1)});
	builder.Link(builder.Operation(Kind::Multiply, color, builder.Texture("baseColorTexture", 0)), output, 0);
	builder.Begin("法線");
	builder.Link(builder.Texture("normalTexture", 0, true), output, 1);
	builder.Begin("メタリック");
	const auto metallic = builder.Parameter("metallic", Type::Float, {.value = 0.0f});
	const auto metal = builder.Operation(Kind::Multiply, metallic, builder.Texture("metallicRoughnessTexture", 4));
	builder.Link(builder.Operation(Kind::Multiply, metal, builder.Texture("metallicTexture", 2)), output, 2);
	builder.Begin("ラフネス");
	const auto roughness = builder.Parameter("roughness", Type::Float, {.value = 0.5f});
	const auto rough = builder.Operation(Kind::Multiply, roughness, builder.Texture("metallicRoughnessTexture", 3));
	const auto roughSample = builder.Operation(Kind::Multiply, rough, builder.Texture("roughnessTexture", 2));
	builder.Link(builder.Operation(Kind::Maximum, roughSample, builder.Constant(0.04f)), output, 3);
	builder.Begin("AO");
	builder.Link(builder.Texture("occlusionTexture", 2), output, 4);
	builder.Begin("発光");
	const auto emissive = builder.Parameter("emissiveColor", Type::Color, {.value = Color4(0, 0, 0, 0)});
	const auto intensity = builder.Parameter("emissiveIntensity", Type::Float, {.value = 0.0f});
	builder.Link(builder.Operation(Kind::Multiply, builder.Operation(Kind::Multiply, emissive, intensity),
					 builder.Texture("emissiveTexture", 1)),
		output, 5);
	builder.Begin("切り抜き");
	builder.Link(builder.Constant(1.0f), output, 6);
	builder.Link(builder.Parameter("alphaClip", Type::Float, {.value = 0.0f}), output, 7);
	// 取り込めない設定が残っていないか確認
	for (const auto& item : material.parameters) {
		Require(builder.consumed_.contains(item.first) ||
					std::any_of(builder.consumed_.begin(), builder.consumed_.end(),
						[&](const auto& name) {
							return ResolveMaterialParameterSemantic(item.first) != MaterialParameterSemantic::None &&
								   ResolveMaterialParameterSemantic(item.first) == ResolveMaterialParameterSemantic(name);
						}),
			"未対応のMaterialパラメータです: " + item.first);
	}
	return builder.graph_;
}

// ノードを配置して接続点を返す
ShaderGraphPBRBuilder::Pin ShaderGraphPBRBuilder::Node(Kind kind) {

	// 現在の領域へノードを追加
	const auto id = UUID::New();
	graph_.nodes.push_back(ShaderGraphNode{
		.id = id, .groupID = group_, .kind = kind, .position = Vector2(column_, row_), .previewExpanded = false});
	column_ += 220.0f;
	return {id, 0};
}

// 出力と入力の接続を追加する
void ShaderGraphPBRBuilder::Link(Pin from, Pin to, uint32_t input) {

	// ノード間の接続を追加
	graph_.links.push_back(ShaderGraphLink{
		.id = UUID::New(), .outputNode = from.node, .outputSlot = from.slot, .inputNode = to.node, .inputSlot = input});
}

// 数値の定数ノードを追加する
ShaderGraphPBRBuilder::Pin ShaderGraphPBRBuilder::Constant(float value) {

	// 定数値をノードへ設定
	auto result = Node(Kind::Constant);
	graph_.nodes.back().value.value = value;
	return result;
}

// 二つの入力を演算ノードへ接続する
ShaderGraphPBRBuilder::Pin ShaderGraphPBRBuilder::Operation(Kind kind, Pin a, Pin b) {

	// 左右の入力を演算へ接続
	auto result = Node(kind);
	Link(a, result, 0);
	Link(b, result, 1);
	return result;
}

// 名前と意味から設定値を解決する
MaterialParameterValue ShaderGraphPBRBuilder::Value(const std::string& name, MaterialParameterValue fallback) {

	// 名前を優先して意味による代替検索
	consumed_.insert(name);
	const auto semantic = ResolveMaterialParameterSemantic(name);
	const auto* value = values_.FindByName(name);
	if (!value && semantic != MaterialParameterSemantic::None) {
		value = values_.Find(semantic);
	}
	return value ? *value : fallback;
}

// 同じ名前の入力を再利用して設定値を登録する
ShaderGraphPBRBuilder::Pin ShaderGraphPBRBuilder::Parameter(
	const std::string& name, Type type, MaterialParameterValue fallback) {

	// 同じ入力名は既存の接続点を再利用
	const auto existing = std::find_if(
		graph_.parameters.begin(), graph_.parameters.end(), [&](const auto& parameter) { return parameter.name == name; });
	if (existing != graph_.parameters.end()) {
		const auto node = std::find_if(graph_.nodes.begin(), graph_.nodes.end(),
			[&](const auto& value) { return value.kind == Kind::Parameter && value.parameterID == existing->id; });
		return {node->id, 0};
	}
	// 値と意味を公開入力へ登録
	auto result = Node(Kind::Parameter);
	const auto id = UUID::New();
	graph_.nodes.back().parameterID = id;
	graph_.parameters.push_back(ShaderGraphParameter{.id = id,
		.name = name,
		.type = type,
		.semantic = ResolveMaterialParameterSemantic(name),
		.defaultValue = ConvertValue(Value(name, fallback), type),
		.referenceName = name});
	return result;
}

// 画像入力とサンプラーと代替値を接続する
ShaderGraphPBRBuilder::Pin ShaderGraphPBRBuilder::Texture(const std::string& name, uint32_t slot, bool normal) {

	const auto value = ConvertValue(Value(name, {.value = AssetID{}}), Type::Texture2D);
	// 未設定でも編集用の入力を残し、サンプルの代替値で見た目を維持する
	const auto parameter = Parameter(name, Type::Texture2D, value);
	auto sample = Node(Kind::TextureSample);
	graph_.nodes.back().value.value = normal ? Vector4(0.5f, 0.5f, 1.0f, 1.0f) : Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	Link(parameter, sample, 0);
	// 画像入力へサンプラーを接続
	Link(sampler_, sample, 2);
	if (normal) {
		// 法線用の色を方向へ変換
		const auto unpack = Node(Kind::NormalUnpack);
		Link(sample, unpack, 0);
		return unpack;
	}
	sample.slot = slot;
	return sample;
}

// 用途別のノード配置領域を追加する
void ShaderGraphPBRBuilder::Begin(const std::string& name) {

	// 次の用途の領域へ配置位置を移動
	row_ += 260.0f;
	column_ = 40.0f;
	group_ = UUID::New();
	graph_.groups.push_back(ShaderGraphGroup{
		.id = group_, .name = name, .position = Vector2(10.0f, row_ - 40.0f), .size = Vector2(2000.0f, 220.0f)});
}
