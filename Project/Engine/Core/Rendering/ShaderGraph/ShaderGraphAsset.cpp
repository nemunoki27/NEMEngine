#include "ShaderGraphAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <array>
#include <utility>

namespace {

	using namespace Engine;

	ShaderGraphParameter MakeStandardParameter(
		std::string_view name, ShaderGraphValueType type,
		MaterialParameterSemantic semantic,
		MaterialParameterValue value) {

		return ShaderGraphParameter{
			.id = Engine::UUID{
				MaterialParameterID::FromName(name).value },
			.name = std::string(name),
			.type = type,
			.semantic = semantic,
			.defaultValue = std::move(value),
		};
	}

	ShaderGraphNode MakeParameterNode(
		const ShaderGraphParameter& parameter, Vector2 position) {

		return ShaderGraphNode{
			.id = Engine::UUID::New(),
			.kind = ShaderGraphNodeKind::Parameter,
			.parameterID = parameter.id,
			.valueType = parameter.type,
			.position = position,
		};
	}

	MaterialParameterValue FloatValue(float value) {

		MaterialParameterValue result{};
		result.value = value;
		return result;
	}

	MaterialParameterValue ColorValue(const Color4& value) {

		MaterialParameterValue result{};
		result.value = value;
		return result;
	}

	MaterialParameterValue TextureValue() {

		MaterialParameterValue result{};
		result.value = AssetID{};
		return result;
	}
}

Engine::ShaderGraphAsset Engine::CreateDefaultSurfaceShaderGraph(
	std::string_view name) {

	ShaderGraphAsset graph{};
	graph.name = name.empty() ? "NewShaderGraph" : std::string(name);
	graph.domain = ShaderGraphDomain::Surface;

	graph.parameters = {
		MakeStandardParameter(
			MaterialParameterNames::BaseColor, ShaderGraphValueType::Color,
			MaterialParameterSemantic::BaseColor,
			ColorValue(Color4(1.0f, 1.0f, 1.0f, 1.0f))),
		MakeStandardParameter(
			MaterialParameterNames::BaseColorTexture, ShaderGraphValueType::Texture2D,
			MaterialParameterSemantic::BaseColorTexture, TextureValue()),
		MakeStandardParameter(
			MaterialParameterNames::NormalTexture, ShaderGraphValueType::Texture2D,
			MaterialParameterSemantic::NormalTexture, TextureValue()),
		MakeStandardParameter(
			MaterialParameterNames::Metallic, ShaderGraphValueType::Float,
			MaterialParameterSemantic::Metallic, FloatValue(0.0f)),
		MakeStandardParameter(
			MaterialParameterNames::MetallicRoughnessTexture, ShaderGraphValueType::Texture2D,
			MaterialParameterSemantic::MetallicTexture, TextureValue()),
		MakeStandardParameter(
			MaterialParameterNames::Roughness, ShaderGraphValueType::Float,
			MaterialParameterSemantic::Roughness, FloatValue(0.5f)),
		MakeStandardParameter(
			MaterialParameterNames::AmbientOcclusion, ShaderGraphValueType::Float,
			MaterialParameterSemantic::AmbientOcclusion, FloatValue(1.0f)),
		MakeStandardParameter(
			MaterialParameterNames::AmbientOcclusionTexture, ShaderGraphValueType::Texture2D,
			MaterialParameterSemantic::AmbientOcclusionTexture, TextureValue()),
		MakeStandardParameter(
			MaterialParameterNames::EmissiveColor, ShaderGraphValueType::Color,
			MaterialParameterSemantic::EmissiveColor,
			ColorValue(Color4(0.0f, 0.0f, 0.0f, 1.0f))),
		MakeStandardParameter(
			MaterialParameterNames::EmissiveTexture, ShaderGraphValueType::Texture2D,
			MaterialParameterSemantic::EmissiveTexture, TextureValue()),
		MakeStandardParameter(
			MaterialParameterNames::EmissiveIntensity, ShaderGraphValueType::Float,
			MaterialParameterSemantic::None, FloatValue(0.0f)),
		MakeStandardParameter(
			MaterialParameterNames::Opacity, ShaderGraphValueType::Float,
			MaterialParameterSemantic::Opacity, FloatValue(1.0f)),
		MakeStandardParameter(
			MaterialParameterNames::AlphaClip, ShaderGraphValueType::Float,
			MaterialParameterSemantic::AlphaClip, FloatValue(0.0f)),
	};

	ShaderGraphNode output{
		.id = Engine::UUID::New(),
		.kind = ShaderGraphNodeKind::SurfaceOutput,
		.position = Vector2(1120.0f, 180.0f),
	};
	graph.outputNode = output.id;
	graph.nodes.emplace_back(output);

	auto addLink =
		[&](UUID source, uint32_t sourceSlot,
			UUID destination, uint32_t destinationSlot) {

		graph.links.emplace_back(ShaderGraphLink{
			.id = Engine::UUID::New(),
			.outputNode = source,
			.outputSlot = sourceSlot,
			.inputNode = destination,
			.inputSlot = destinationSlot,
			});
	};
	auto addNode =
		[&](ShaderGraphNodeKind kind, Vector2 position,
			const Color4& fallback = Color4::White()) {

		ShaderGraphNode node{
			.id = Engine::UUID::New(),
			.kind = kind,
			.position = position,
		};
		if (kind == ShaderGraphNodeKind::TextureSample) {
			node.value = ColorValue(fallback);
		}
		const UUID id = node.id;
		graph.nodes.emplace_back(std::move(node));
		return id;
	};
	auto addParameter =
		[&](uint32_t index, Vector2 position) {

		ShaderGraphNode node =
			MakeParameterNode(
				graph.parameters[index],
				position);
		const UUID id = node.id;
		graph.nodes.emplace_back(std::move(node));
		return id;
	};

	const UUID uv =
		addNode(
			ShaderGraphNodeKind::UV,
			Vector2(20.0f, 420.0f));

	// 標準PBRの係数とテクスチャを最初から接続し、未設定テクスチャは各Sampleのfallbackへ戻す
	const UUID baseColor =
		addParameter(0, Vector2(20.0f, 20.0f));
	const UUID baseTexture =
		addParameter(1, Vector2(20.0f, 140.0f));
	const UUID baseSample =
		addNode(
			ShaderGraphNodeKind::TextureSample,
			Vector2(300.0f, 100.0f));
	const UUID baseMultiply =
		addNode(
			ShaderGraphNodeKind::Multiply,
			Vector2(680.0f, 60.0f));
	addLink(baseTexture, 0, baseSample, 0);
	addLink(uv, 0, baseSample, 1);
	addLink(baseColor, 0, baseMultiply, 0);
	addLink(baseSample, 0, baseMultiply, 1);
	addLink(baseMultiply, 0, output.id, 0);

	const UUID normalTexture =
		addParameter(2, Vector2(20.0f, 560.0f));
	const UUID normalSample =
		addNode(
			ShaderGraphNodeKind::TextureSample,
			Vector2(300.0f, 520.0f),
			Color4(0.5f, 0.5f, 1.0f, 1.0f));
	const UUID normalUnpack =
		addNode(
			ShaderGraphNodeKind::NormalUnpack,
			Vector2(680.0f, 520.0f));
	addLink(normalTexture, 0, normalSample, 0);
	addLink(uv, 0, normalSample, 1);
	addLink(normalSample, 0, normalUnpack, 0);
	addLink(normalUnpack, 0, output.id, 1);

	const UUID metallic =
		addParameter(3, Vector2(20.0f, 820.0f));
	const UUID metallicRoughnessTexture =
		addParameter(4, Vector2(20.0f, 940.0f));
	const UUID roughness =
		addParameter(5, Vector2(20.0f, 1060.0f));
	const UUID metallicRoughnessSample =
		addNode(
			ShaderGraphNodeKind::TextureSample,
			Vector2(300.0f, 900.0f));
	const UUID metallicMultiply =
		addNode(
			ShaderGraphNodeKind::Multiply,
			Vector2(680.0f, 820.0f));
	const UUID roughnessMultiply =
		addNode(
			ShaderGraphNodeKind::Multiply,
			Vector2(680.0f, 1020.0f));
	addLink(
		metallicRoughnessTexture, 0,
		metallicRoughnessSample, 0);
	addLink(uv, 0, metallicRoughnessSample, 1);
	addLink(metallic, 0, metallicMultiply, 0);
	addLink(
		metallicRoughnessSample, 4,
		metallicMultiply, 1);
	addLink(metallicMultiply, 0, output.id, 2);
	addLink(roughness, 0, roughnessMultiply, 0);
	addLink(
		metallicRoughnessSample, 3,
		roughnessMultiply, 1);
	addLink(roughnessMultiply, 0, output.id, 3);

	const UUID ambientOcclusion =
		addParameter(6, Vector2(20.0f, 1260.0f));
	const UUID occlusionTexture =
		addParameter(7, Vector2(20.0f, 1380.0f));
	const UUID occlusionSample =
		addNode(
			ShaderGraphNodeKind::TextureSample,
			Vector2(300.0f, 1340.0f));
	const UUID occlusionMultiply =
		addNode(
			ShaderGraphNodeKind::Multiply,
			Vector2(680.0f, 1300.0f));
	addLink(occlusionTexture, 0, occlusionSample, 0);
	addLink(uv, 0, occlusionSample, 1);
	addLink(
		ambientOcclusion, 0,
		occlusionMultiply, 0);
	addLink(occlusionSample, 2, occlusionMultiply, 1);
	addLink(occlusionMultiply, 0, output.id, 4);

	const UUID emissiveColor =
		addParameter(8, Vector2(20.0f, 1620.0f));
	const UUID emissiveTexture =
		addParameter(9, Vector2(20.0f, 1740.0f));
	const UUID emissiveIntensity =
		addParameter(10, Vector2(20.0f, 1860.0f));
	const UUID emissiveSample =
		addNode(
			ShaderGraphNodeKind::TextureSample,
			Vector2(300.0f, 1700.0f));
	const UUID emissiveTextureMultiply =
		addNode(
			ShaderGraphNodeKind::Multiply,
			Vector2(600.0f, 1640.0f));
	const UUID emissiveIntensityMultiply =
		addNode(
			ShaderGraphNodeKind::Multiply,
			Vector2(820.0f, 1640.0f));
	addLink(emissiveTexture, 0, emissiveSample, 0);
	addLink(uv, 0, emissiveSample, 1);
	addLink(
		emissiveColor, 0,
		emissiveTextureMultiply, 0);
	addLink(
		emissiveSample, 1,
		emissiveTextureMultiply, 1);
	addLink(
		emissiveTextureMultiply, 0,
		emissiveIntensityMultiply, 0);
	addLink(
		emissiveIntensity, 0,
		emissiveIntensityMultiply, 1);
	addLink(emissiveIntensityMultiply, 0, output.id, 5);

	const UUID opacity =
		addParameter(11, Vector2(760.0f, 1900.0f));
	const UUID alphaClip =
		addParameter(12, Vector2(760.0f, 2020.0f));
	addLink(opacity, 0, output.id, 6);
	addLink(alphaClip, 0, output.id, 7);
	return graph;
}

std::string_view Engine::GetShaderGraphNodeName(ShaderGraphNodeKind kind) {

	switch (kind) {
	case ShaderGraphNodeKind::SurfaceOutput: return "PBR Surface";
	case ShaderGraphNodeKind::PostProcessOutput: return "Post Process";
	case ShaderGraphNodeKind::Parameter: return "Parameter";
	case ShaderGraphNodeKind::Constant: return "Constant";
	case ShaderGraphNodeKind::UV: return "UV";
	case ShaderGraphNodeKind::WorldNormal: return "World Normal";
	case ShaderGraphNodeKind::WorldPosition: return "World Position";
	case ShaderGraphNodeKind::Add: return "Add";
	case ShaderGraphNodeKind::Multiply: return "Multiply";
	case ShaderGraphNodeKind::Lerp: return "Lerp";
	case ShaderGraphNodeKind::OneMinus: return "One Minus";
	case ShaderGraphNodeKind::Saturate: return "Saturate";
	case ShaderGraphNodeKind::TextureSample: return "Sample Texture 2D";
	case ShaderGraphNodeKind::NormalUnpack: return "Unpack Normal";
	case ShaderGraphNodeKind::SceneColor: return "Scene Color";
	}
	return "Unknown";
}

uint32_t Engine::GetShaderGraphInputCount(ShaderGraphNodeKind kind) {

	switch (kind) {
	case ShaderGraphNodeKind::SurfaceOutput: return 8;
	case ShaderGraphNodeKind::PostProcessOutput: return 1;
	case ShaderGraphNodeKind::Add:
	case ShaderGraphNodeKind::Multiply: return 2;
	case ShaderGraphNodeKind::Lerp: return 3;
	case ShaderGraphNodeKind::OneMinus:
	case ShaderGraphNodeKind::Saturate:
	case ShaderGraphNodeKind::NormalUnpack: return 1;
	case ShaderGraphNodeKind::TextureSample: return 2;
	default: return 0;
	}
}

uint32_t Engine::GetShaderGraphOutputCount(ShaderGraphNodeKind kind) {

	switch (kind) {
	case ShaderGraphNodeKind::SurfaceOutput:
	case ShaderGraphNodeKind::PostProcessOutput:
		return 0;
	case ShaderGraphNodeKind::TextureSample:
	case ShaderGraphNodeKind::SceneColor:
		return 6;
	default:
		return 1;
	}
}

std::string_view Engine::GetShaderGraphInputName(
	ShaderGraphNodeKind kind, uint32_t slot) {

	static constexpr std::array surfaceNames{
		"Base Color", "Normal", "Metallic", "Roughness",
		"Ambient Occlusion", "Emissive", "Opacity", "Alpha Clip",
	};
	static constexpr std::array binaryNames{ "A", "B" };
	static constexpr std::array lerpNames{ "A", "B", "T" };
	static constexpr std::array textureNames{ "Texture", "UV" };

	if (kind == ShaderGraphNodeKind::SurfaceOutput && slot < surfaceNames.size()) {
		return surfaceNames[slot];
	}
	if (kind == ShaderGraphNodeKind::PostProcessOutput) {
		return "Color";
	}
	if ((kind == ShaderGraphNodeKind::Add ||
		kind == ShaderGraphNodeKind::Multiply) &&
		slot < binaryNames.size()) {

		return binaryNames[slot];
	}
	if (kind == ShaderGraphNodeKind::Lerp && slot < lerpNames.size()) {
		return lerpNames[slot];
	}
	if (kind == ShaderGraphNodeKind::TextureSample && slot < textureNames.size()) {
		return textureNames[slot];
	}
	if (GetShaderGraphInputCount(kind) == 1) {
		return "Input";
	}
	return "";
}

std::string_view Engine::GetShaderGraphOutputName(
	ShaderGraphNodeKind kind, uint32_t slot) {

	static constexpr std::array textureNames{
		"RGBA", "RGB", "R", "G", "B", "A",
	};
	if ((kind == ShaderGraphNodeKind::TextureSample ||
		kind == ShaderGraphNodeKind::SceneColor) &&
		slot < textureNames.size()) {

		return textureNames[slot];
	}
	return slot == 0 ? "Output" : "";
}

bool Engine::FromJson(const nlohmann::json& data, ShaderGraphAsset& outAsset) {

	if (!data.is_object()) {
		return false;
	}

	outAsset = ShaderGraphAsset{};
	outAsset.name = data.value("name", "NewShaderGraph");
	outAsset.domain =
		EnumAdapter<ShaderGraphDomain>::FromString(
			data.value("domain", "Surface")).
		value_or(ShaderGraphDomain::Surface);
	outAsset.surfaceMode =
		EnumAdapter<ShaderGraphSurfaceMode>::FromString(
			data.value("surfaceMode", "Opaque")).
		value_or(ShaderGraphSurfaceMode::Opaque);
	outAsset.outputNode =
		FromString16Hex(data.value("outputNode", ""));
	outAsset.generatedMaterial =
		ParseAssetID(data, "generatedMaterial");
	outAsset.generatedOpaqueShader =
		ParseAssetID(data, "generatedOpaqueShader");
	outAsset.generatedTransparentShader =
		ParseAssetID(data, "generatedTransparentShader");

	if (data.contains("parameters") && data["parameters"].is_array()) {
		for (const nlohmann::json& item : data["parameters"]) {

			ShaderGraphParameter parameter{};
			parameter.id = FromString16Hex(item.value("id", ""));
			parameter.name = item.value("name", "");
			parameter.type =
				EnumAdapter<ShaderGraphValueType>::FromString(
					item.value("type", "Float")).
				value_or(ShaderGraphValueType::Float);
			parameter.semantic =
				EnumAdapter<MaterialParameterSemantic>::FromString(
					item.value("semantic", "None")).
				value_or(MaterialParameterSemantic::None);
			if (item.contains("defaultValue")) {
				ParseMaterialParameterValue(
					item["defaultValue"], parameter.defaultValue);
			}
			if (parameter.id && !parameter.name.empty()) {
				outAsset.parameters.emplace_back(std::move(parameter));
			}
		}
	}

	if (data.contains("nodes") && data["nodes"].is_array()) {
		for (const nlohmann::json& item : data["nodes"]) {

			ShaderGraphNode node{};
			node.id = FromString16Hex(item.value("id", ""));
			node.kind =
				EnumAdapter<ShaderGraphNodeKind>::FromString(
					item.value("kind", "Constant")).
				value_or(ShaderGraphNodeKind::Constant);
			node.parameterID =
				FromString16Hex(item.value("parameterID", ""));
			node.valueType =
				EnumAdapter<ShaderGraphValueType>::FromString(
					item.value("valueType", "Float")).
				value_or(ShaderGraphValueType::Float);
			if (item.contains("value")) {
				ParseMaterialParameterValue(item["value"], node.value);
			}
			if (node.kind == ShaderGraphNodeKind::TextureSample &&
				!std::holds_alternative<Color4>(node.value.value) &&
				!std::holds_alternative<Vector4>(node.value.value)) {

				// fallback未保存の旧グラフは未設定Textureを白として扱う
				node.value = ColorValue(Color4::White());
			}
			node.position =
				Vector2::FromJson(item.value("position", nlohmann::json{}));
			if (node.id) {
				outAsset.nodes.emplace_back(std::move(node));
			}
		}
	}

	if (data.contains("links") && data["links"].is_array()) {
		for (const nlohmann::json& item : data["links"]) {

			ShaderGraphLink link{};
			link.id = FromString16Hex(item.value("id", ""));
			link.outputNode =
				FromString16Hex(item.value("outputNode", ""));
			link.outputSlot = item.value("outputSlot", 0u);
			link.inputNode =
				FromString16Hex(item.value("inputNode", ""));
			link.inputSlot = item.value("inputSlot", 0u);
			if (link.id && link.outputNode && link.inputNode) {
				outAsset.links.emplace_back(std::move(link));
			}
		}
	}
	return outAsset.outputNode && !outAsset.nodes.empty();
}

nlohmann::json Engine::ToJson(const ShaderGraphAsset& asset) {

	nlohmann::json data{
		{ "schemaVersion", 1 },
		{ "name", asset.name },
		{ "domain", EnumAdapter<ShaderGraphDomain>::ToString(asset.domain) },
		{ "surfaceMode", EnumAdapter<ShaderGraphSurfaceMode>::ToString(asset.surfaceMode) },
		{ "outputNode", asset.outputNode ? ToString(asset.outputNode) : "" },
		{ "generatedMaterial", ToAssetReferenceJson(asset.generatedMaterial) },
		{ "generatedOpaqueShader", ToAssetReferenceJson(asset.generatedOpaqueShader) },
		{ "generatedTransparentShader", ToAssetReferenceJson(asset.generatedTransparentShader) },
		{ "parameters", nlohmann::json::array() },
		{ "nodes", nlohmann::json::array() },
		{ "links", nlohmann::json::array() },
	};

	for (const ShaderGraphParameter& parameter : asset.parameters) {
		data["parameters"].push_back({
			{ "id", ToString(parameter.id) },
			{ "name", parameter.name },
			{ "type", EnumAdapter<ShaderGraphValueType>::ToString(parameter.type) },
			{ "semantic", EnumAdapter<MaterialParameterSemantic>::ToString(parameter.semantic) },
			{ "defaultValue", SerializeMaterialParameterValue(parameter.defaultValue) },
			});
	}
	for (const ShaderGraphNode& node : asset.nodes) {
		data["nodes"].push_back({
			{ "id", ToString(node.id) },
			{ "kind", EnumAdapter<ShaderGraphNodeKind>::ToString(node.kind) },
			{ "parameterID", node.parameterID ? ToString(node.parameterID) : "" },
			{ "valueType", EnumAdapter<ShaderGraphValueType>::ToString(node.valueType) },
			{ "value", SerializeMaterialParameterValue(node.value) },
			{ "position", node.position.ToJson() },
			});
	}
	for (const ShaderGraphLink& link : asset.links) {
		data["links"].push_back({
			{ "id", ToString(link.id) },
			{ "outputNode", ToString(link.outputNode) },
			{ "outputSlot", link.outputSlot },
			{ "inputNode", ToString(link.inputNode) },
			{ "inputSlot", link.inputSlot },
			});
	}
	return data;
}
