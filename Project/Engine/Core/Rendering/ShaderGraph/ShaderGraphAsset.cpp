#include "ShaderGraphAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>

// c++
#include <array>
#include <limits>
#include <utility>

namespace {

	using namespace Engine;

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

	ShaderGraphNode MakeConstantNode(
		ShaderGraphValueType type,
		MaterialParameterValue value,
		Vector2 position) {

		return ShaderGraphNode{
			.id = Engine::UUID::New(),
			.kind = ShaderGraphNodeKind::Constant,
			.valueType = type,
			.value = std::move(value),
			.position = position,
			.previewExpanded = false,
		};
	}

	void ReadPorts(const nlohmann::json& data,
		std::vector<ShaderGraphPort>& outPorts) {

		if (!data.is_array()) {
			return;
		}
		for (const nlohmann::json& item : data) {

			ShaderGraphPort port{};
			port.id = FromString16Hex(item.value("id", ""));
			port.name = item.value("name", "");
			port.type =
				EnumAdapter<ShaderGraphValueType>::FromString(
					item.value("type", "Float")).
				value_or(ShaderGraphValueType::Float);
			if (item.contains("defaultValue")) {
				ParseMaterialParameterValue(
					item["defaultValue"], port.defaultValue);
			}
			if (!port.id) {
				port.id = Engine::UUID::New();
			}
			if (!port.name.empty()) {
				outPorts.emplace_back(std::move(port));
			}
		}
	}

	nlohmann::json WritePorts(
		const std::vector<ShaderGraphPort>& ports) {

		nlohmann::json data = nlohmann::json::array();
		for (const ShaderGraphPort& port : ports) {
			data.push_back({
				{ "id", ToString(port.id) },
				{ "name", port.name },
				{ "type", EnumAdapter<ShaderGraphValueType>::ToString(port.type) },
				{ "defaultValue", SerializeMaterialParameterValue(port.defaultValue) },
				});
		}
		return data;
	}

	void ReadSampler(const nlohmann::json& data,
		PipelineStaticSamplerSettings& outSampler) {

		if (!data.is_object()) {
			return;
		}
		outSampler.filter =
			EnumAdapter<D3D12_FILTER>::FromString(
				data.value("filter", "D3D12_FILTER_MIN_MAG_MIP_LINEAR")).
			value_or(D3D12_FILTER_MIN_MAG_MIP_LINEAR);
		outSampler.addressU =
			EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::FromString(
				data.value("addressU", "D3D12_TEXTURE_ADDRESS_MODE_WRAP")).
			value_or(D3D12_TEXTURE_ADDRESS_MODE_WRAP);
		outSampler.addressV =
			EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::FromString(
				data.value("addressV", "D3D12_TEXTURE_ADDRESS_MODE_WRAP")).
			value_or(D3D12_TEXTURE_ADDRESS_MODE_WRAP);
		outSampler.addressW =
			EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::FromString(
				data.value("addressW", "D3D12_TEXTURE_ADDRESS_MODE_WRAP")).
			value_or(D3D12_TEXTURE_ADDRESS_MODE_WRAP);
		outSampler.comparisonFunc =
			EnumAdapter<D3D12_COMPARISON_FUNC>::FromString(
				data.value("comparisonFunc", "D3D12_COMPARISON_FUNC_ALWAYS")).
			value_or(D3D12_COMPARISON_FUNC_ALWAYS);
		outSampler.borderColor =
			EnumAdapter<D3D12_STATIC_BORDER_COLOR>::FromString(
				data.value("borderColor", "D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK")).
			value_or(D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);
		outSampler.maxAnisotropy = data.value("maxAnisotropy", 1u);
		outSampler.mipLODBias = data.value("mipLODBias", 0.0f);
		outSampler.minLOD = data.value("minLOD", 0.0f);
		outSampler.maxLOD = data.value("maxLOD", D3D12_FLOAT32_MAX);
	}

	nlohmann::json WriteSampler(
		const PipelineStaticSamplerSettings& sampler) {

		return {
			{ "filter", EnumAdapter<D3D12_FILTER>::ToString(sampler.filter) },
			{ "addressU", EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::ToString(sampler.addressU) },
			{ "addressV", EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::ToString(sampler.addressV) },
			{ "addressW", EnumAdapter<D3D12_TEXTURE_ADDRESS_MODE>::ToString(sampler.addressW) },
			{ "comparisonFunc", EnumAdapter<D3D12_COMPARISON_FUNC>::ToString(sampler.comparisonFunc) },
			{ "borderColor", EnumAdapter<D3D12_STATIC_BORDER_COLOR>::ToString(sampler.borderColor) },
			{ "maxAnisotropy", sampler.maxAnisotropy },
			{ "mipLODBias", sampler.mipLODBias },
			{ "minLOD", sampler.minLOD },
			{ "maxLOD", sampler.maxLOD },
		};
	}

	void SetDefaultSampler(PipelineStaticSamplerSettings& sampler) {

		sampler.addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	}
}

Engine::ShaderGraphAsset Engine::CreateDefaultSurfaceShaderGraph(
	std::string_view name,
	ShaderGraphTarget target) {

	ShaderGraphAsset graph{};
	graph.name = name.empty() ? "NewShaderGraph" : std::string(name);
	graph.domain = ShaderGraphDomain::Surface;
	graph.target = target;
	if (target == ShaderGraphTarget::Particle ||
		target == ShaderGraphTarget::Trail) {
		graph.surfaceMode = ShaderGraphSurfaceMode::Transparent;
		graph.renderState.depthWrite = false;
		graph.renderState.cullMode = D3D12_CULL_MODE_NONE;
	}

	ShaderGraphNode output{
		.id = Engine::UUID::New(),
		.kind = IsShaderGraph3DTarget(target) ?
			ShaderGraphNodeKind::SurfaceOutput :
			ShaderGraphNodeKind::UnlitOutput,
		.position = Vector2(520.0f, 120.0f),
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
	auto addConstant =
		[&](ShaderGraphValueType type,
			MaterialParameterValue value,
			Vector2 position) {

		ShaderGraphNode node =
			MakeConstantNode(type, std::move(value), position);
		const UUID id = node.id;
		graph.nodes.emplace_back(std::move(node));
		return id;
	};

	const UUID baseColor = addConstant(
		ShaderGraphValueType::Color,
		ColorValue(Color4::White()),
		Vector2(40.0f, 20.0f));
	addLink(baseColor, 0, output.id, 0);

	if (IsShaderGraph3DTarget(target)) {
		// 法線は未接続にし、VS/MSが出力した幾何法線を使用する
		const UUID metallic = addConstant(
			ShaderGraphValueType::Float,
			FloatValue(0.0f),
			Vector2(40.0f, 140.0f));
		const UUID roughness = addConstant(
			ShaderGraphValueType::Float,
			FloatValue(0.5f),
			Vector2(40.0f, 230.0f));
		const UUID ambientOcclusion = addConstant(
			ShaderGraphValueType::Float,
			FloatValue(1.0f),
			Vector2(40.0f, 320.0f));
		const UUID emissive = addConstant(
			ShaderGraphValueType::Color,
			ColorValue(Color4(0.0f, 0.0f, 0.0f, 1.0f)),
			Vector2(40.0f, 410.0f));
		const UUID opacity = addConstant(
			ShaderGraphValueType::Float,
			FloatValue(1.0f),
			Vector2(40.0f, 530.0f));
		const UUID alphaClip = addConstant(
			ShaderGraphValueType::Float,
			FloatValue(0.0f),
			Vector2(40.0f, 620.0f));
		addLink(metallic, 0, output.id, 2);
		addLink(roughness, 0, output.id, 3);
		addLink(ambientOcclusion, 0, output.id, 4);
		addLink(emissive, 0, output.id, 5);
		addLink(opacity, 0, output.id, 6);
		addLink(alphaClip, 0, output.id, 7);
	} else {
		const UUID opacity = addConstant(
			ShaderGraphValueType::Float,
			FloatValue(1.0f),
			Vector2(40.0f, 140.0f));
		const UUID alphaClip = addConstant(
			ShaderGraphValueType::Float,
			FloatValue(0.0f),
			Vector2(40.0f, 230.0f));
		addLink(opacity, 0, output.id, 1);
		addLink(alphaClip, 0, output.id, 2);
	}
	return graph;
}

Engine::ShaderGraphAsset Engine::CreateDefaultPostProcessShaderGraph(
	std::string_view name) {

	ShaderGraphAsset graph{};
	graph.name = name.empty() ?
		"NewPostProcessGraph" : std::string(name);
	graph.domain = ShaderGraphDomain::PostProcess;

	ShaderGraphNode output{
		.id = Engine::UUID::New(),
		.kind = ShaderGraphNodeKind::PostProcessOutput,
		.position = Vector2(480.0f, 120.0f),
	};
	ShaderGraphNode sceneColor{
		.id = Engine::UUID::New(),
		.kind = ShaderGraphNodeKind::SceneColor,
		.position = Vector2(80.0f, 120.0f),
	};
	graph.outputNode = output.id;
	graph.nodes.emplace_back(output);
	graph.nodes.emplace_back(sceneColor);
	graph.links.emplace_back(ShaderGraphLink{
		.id = Engine::UUID::New(),
		.outputNode = sceneColor.id,
		.outputSlot = 0,
		.inputNode = output.id,
		.inputSlot = 0,
		});
	return graph;
}

bool Engine::IsShaderGraph3DTarget(
	ShaderGraphTarget target) {

	return target == ShaderGraphTarget::Mesh ||
		target == ShaderGraphTarget::Primitive3D;
}

bool Engine::SupportsShaderGraphVertexOutput(
	ShaderGraphTarget target) {

	return target == ShaderGraphTarget::Mesh ||
		target == ShaderGraphTarget::Primitive3D ||
		target == ShaderGraphTarget::Primitive2D;
}

std::string_view Engine::GetShaderGraphNodeName(ShaderGraphNodeKind kind) {

	const ShaderGraphNodeDescriptor* descriptor =
		ShaderGraphNodeRegistry::Find(kind);
	return descriptor ? descriptor->name : "Unknown";
}

uint32_t Engine::GetShaderGraphInputCount(ShaderGraphNodeKind kind) {

	const ShaderGraphNodeDescriptor* descriptor =
		ShaderGraphNodeRegistry::Find(kind);
	return descriptor ? static_cast<uint32_t>(descriptor->inputs.size()) : 0u;
}

uint32_t Engine::GetShaderGraphOutputCount(ShaderGraphNodeKind kind) {

	const ShaderGraphNodeDescriptor* descriptor =
		ShaderGraphNodeRegistry::Find(kind);
	return descriptor ? static_cast<uint32_t>(descriptor->outputs.size()) : 0u;
}

std::string_view Engine::GetShaderGraphInputName(
	ShaderGraphNodeKind kind, uint32_t slot) {

	const ShaderGraphNodeDescriptor* descriptor =
		ShaderGraphNodeRegistry::Find(kind);
	return descriptor && slot < descriptor->inputs.size() ?
		descriptor->inputs[slot].name : std::string_view{};
}

std::string_view Engine::GetShaderGraphOutputName(
	ShaderGraphNodeKind kind, uint32_t slot) {

	const ShaderGraphNodeDescriptor* descriptor =
		ShaderGraphNodeRegistry::Find(kind);
	return descriptor && slot < descriptor->outputs.size() ?
		descriptor->outputs[slot].name : std::string_view{};
}

uint32_t Engine::GetShaderGraphInputCount(
	const ShaderGraphNode& node) {

	return node.inputPorts.empty() ?
		GetShaderGraphInputCount(node.kind) :
		static_cast<uint32_t>(node.inputPorts.size());
}

uint32_t Engine::GetShaderGraphOutputCount(
	const ShaderGraphNode& node) {

	return node.outputPorts.empty() ?
		GetShaderGraphOutputCount(node.kind) :
		static_cast<uint32_t>(node.outputPorts.size());
}

std::string_view Engine::GetShaderGraphInputName(
	const ShaderGraphNode& node, uint32_t slot) {

	if (!node.inputPorts.empty()) {
		return slot < node.inputPorts.size() ?
			node.inputPorts[slot].name : std::string_view{};
	}
	return GetShaderGraphInputName(node.kind, slot);
}

std::string_view Engine::GetShaderGraphOutputName(
	const ShaderGraphNode& node, uint32_t slot) {

	if (!node.outputPorts.empty()) {
		return slot < node.outputPorts.size() ?
			node.outputPorts[slot].name : std::string_view{};
	}
	return GetShaderGraphOutputName(node.kind, slot);
}

bool Engine::FromJson(const nlohmann::json& data, ShaderGraphAsset& outAsset) {

	if (!data.is_object()) {
		return false;
	}

	outAsset = ShaderGraphAsset{};
	const uint32_t schemaVersion = data.value("schemaVersion", 0u);
	outAsset.name = data.value("name", "NewShaderGraph");
	outAsset.domain =
		EnumAdapter<ShaderGraphDomain>::FromString(
			data.value("domain", "Surface")).
		value_or(ShaderGraphDomain::Surface);
	outAsset.surfaceMode =
		EnumAdapter<ShaderGraphSurfaceMode>::FromString(
			data.value("surfaceMode", "Opaque")).
		value_or(ShaderGraphSurfaceMode::Opaque);
	outAsset.target =
		EnumAdapter<ShaderGraphTarget>::FromString(
			data.value("target", "Mesh")).
		value_or(ShaderGraphTarget::Mesh);
	outAsset.defaultPrecision =
		EnumAdapter<ShaderGraphPrecision>::FromString(
			data.value("defaultPrecision", "Float")).
		value_or(ShaderGraphPrecision::Float);
	if (data.contains("renderState") &&
		data["renderState"].is_object()) {

		const nlohmann::json& renderState = data["renderState"];
		outAsset.renderState.twoSided =
			renderState.value("twoSided", false);
		outAsset.renderState.depthWrite =
			renderState.value("depthWrite", true);
		outAsset.renderState.depthTest =
			renderState.value("depthTest", true);
		outAsset.renderState.alphaClipping =
			renderState.value("alphaClipping", false);
		outAsset.renderState.castShadows =
			renderState.value("castShadows", true);
		outAsset.renderState.receiveShadows =
			renderState.value("receiveShadows", true);
		outAsset.renderState.blendMode =
			EnumAdapter<BlendMode>::FromString(
				renderState.value("blendMode", "Normal")).
			value_or(BlendMode::Normal);
		outAsset.renderState.fillMode =
			EnumAdapter<D3D12_FILL_MODE>::FromString(
				renderState.value("fillMode", "D3D12_FILL_MODE_SOLID")).
			value_or(D3D12_FILL_MODE_SOLID);
		outAsset.renderState.cullMode =
			EnumAdapter<D3D12_CULL_MODE>::FromString(
				renderState.value("cullMode", "D3D12_CULL_MODE_BACK")).
			value_or(D3D12_CULL_MODE_BACK);
		outAsset.renderState.frontCounterClockwise =
			renderState.value("frontCounterClockwise", false);
		outAsset.renderState.depthClipEnable =
			renderState.value("depthClipEnable", true);
		outAsset.renderState.depthFunc =
			EnumAdapter<D3D12_COMPARISON_FUNC>::FromString(
				renderState.value("depthFunc", "D3D12_COMPARISON_FUNC_LESS_EQUAL")).
			value_or(D3D12_COMPARISON_FUNC_LESS_EQUAL);
		outAsset.renderState.stencilEnable =
			renderState.value("stencilEnable", false);
	}
	outAsset.outputNode =
		FromString16Hex(data.value("outputNode", ""));
	outAsset.vertexOutputNode =
		FromString16Hex(data.value("vertexOutputNode", ""));

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
			parameter.precision =
				EnumAdapter<ShaderGraphPrecision>::FromString(
					item.value("precision", "Inherit")).
				value_or(ShaderGraphPrecision::Inherit);
			parameter.scope =
				EnumAdapter<ShaderGraphParameterScope>::FromString(
					item.value("scope", "PerMaterial")).
				value_or(ShaderGraphParameterScope::PerMaterial);
			parameter.exposed = item.value("exposed", true);
			parameter.referenceName =
				item.value("referenceName", parameter.name);
			if (item.contains("defaultValue")) {
				ParseMaterialParameterValue(
					item["defaultValue"], parameter.defaultValue);
			}
			if (parameter.id && !parameter.name.empty()) {
				outAsset.parameters.emplace_back(std::move(parameter));
			}
		}
	}

	if (data.contains("keywords") && data["keywords"].is_array()) {
		for (const nlohmann::json& item : data["keywords"]) {

			ShaderGraphKeyword keyword{};
			keyword.id = FromString16Hex(item.value("id", ""));
			keyword.name = item.value("name", "");
			keyword.referenceName =
				item.value("referenceName", keyword.name);
			keyword.type =
				EnumAdapter<ShaderGraphKeywordType>::FromString(
					item.value("type", "Boolean")).
				value_or(ShaderGraphKeywordType::Boolean);
			keyword.defaultIndex = item.value("defaultIndex", 0u);
			keyword.runtimeToggle = item.value("runtimeToggle", false);
			if (item.contains("entries") && item["entries"].is_array()) {
				keyword.entries = item["entries"].get<std::vector<std::string>>();
			}
			if (keyword.id && !keyword.name.empty()) {
				outAsset.keywords.emplace_back(std::move(keyword));
			}
		}
	}

	if (data.contains("nodes") && data["nodes"].is_array()) {
		for (const nlohmann::json& item : data["nodes"]) {

			ShaderGraphNode node{};
			node.id = FromString16Hex(item.value("id", ""));
			node.groupID =
				FromString16Hex(item.value("groupID", ""));
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
			if (node.kind == ShaderGraphNodeKind::SamplerState) {
				SetDefaultSampler(node.sampler);
				ReadSampler(item.value("sampler", nlohmann::json{}),
					node.sampler);
			}
			node.position =
				Vector2::FromJson(item.value("position", nlohmann::json{}));
			node.stage =
				EnumAdapter<ShaderGraphStage>::FromString(
					item.value("stage", "Any")).
				value_or(ShaderGraphStage::Any);
			node.precision =
				EnumAdapter<ShaderGraphPrecision>::FromString(
					item.value("precision", "Inherit")).
				value_or(ShaderGraphPrecision::Inherit);
			ReadPorts(item.value("inputPorts", nlohmann::json::array()),
				node.inputPorts);
			ReadPorts(item.value("outputPorts", nlohmann::json::array()),
				node.outputPorts);
			node.subGraph = ParseAssetID(item, "subGraph");
			node.keywordID =
				FromString16Hex(item.value("keywordID", ""));
			node.customFunctionSource =
				EnumAdapter<ShaderGraphCustomFunctionSource>::FromString(
					item.value("customFunctionSource", "Inline")).
				value_or(ShaderGraphCustomFunctionSource::Inline);
			node.functionName = item.value("functionName", "");
			node.functionFileAsset =
				ParseAssetID(item, "functionFileAsset");
			node.functionFile = item.value("functionFile", "");
			node.functionBody = item.value("functionBody", "");
			node.previewExpanded =
				item.value(
					"previewExpanded",
					node.kind !=
						ShaderGraphNodeKind::Parameter &&
					node.kind !=
						ShaderGraphNodeKind::Constant);
			if (node.id) {
				outAsset.nodes.emplace_back(std::move(node));
			}
		}
	}

	if (data.contains("groups") && data["groups"].is_array()) {
		for (const nlohmann::json& item : data["groups"]) {

			ShaderGraphGroup group{};
			group.id = FromString16Hex(item.value("id", ""));
			group.name = item.value("name", "Group");
			group.position =
				Vector2::FromJson(item.value("position", nlohmann::json{}));
			group.size =
				Vector2::FromJson(item.value("size", nlohmann::json{}));
			if (group.id &&
				0.0f < group.size.x &&
				0.0f < group.size.y) {

				outAsset.groups.emplace_back(std::move(group));
			}
		}
	}
	if (schemaVersion < 7 && !outAsset.groups.empty()) {

		// 所属IDがないグラフは最も近い包含グループへ一度だけ移行
		for (ShaderGraphNode& node : outAsset.nodes) {
			if (node.groupID) {
				continue;
			}

			const ShaderGraphGroup* nearestGroup = nullptr;
			float nearestDistance =
				(std::numeric_limits<float>::max)();
			for (const ShaderGraphGroup& group : outAsset.groups) {
				const Vector2 maximum = group.position + group.size;
				if (node.position.x < group.position.x ||
					node.position.y < group.position.y ||
					maximum.x < node.position.x ||
					maximum.y < node.position.y) {

					continue;
				}

				const Vector2 center =
					group.position + group.size * 0.5f;
				const Vector2 offset = node.position - center;
				const float distance =
					offset.x * offset.x + offset.y * offset.y;
				if (distance < nearestDistance) {
					nearestDistance = distance;
					nearestGroup = &group;
				}
			}
			if (nearestGroup) {
				node.groupID = nearestGroup->id;
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
		{ "schemaVersion", 8 },
		{ "name", asset.name },
		{ "domain", EnumAdapter<ShaderGraphDomain>::ToString(asset.domain) },
		{ "surfaceMode", EnumAdapter<ShaderGraphSurfaceMode>::ToString(asset.surfaceMode) },
		{ "target", EnumAdapter<ShaderGraphTarget>::ToString(asset.target) },
		{ "defaultPrecision", EnumAdapter<ShaderGraphPrecision>::ToString(asset.defaultPrecision) },
		{ "renderState", {
			{ "twoSided", asset.renderState.twoSided },
			{ "depthWrite", asset.renderState.depthWrite },
			{ "depthTest", asset.renderState.depthTest },
			{ "alphaClipping", asset.renderState.alphaClipping },
			{ "castShadows", asset.renderState.castShadows },
			{ "receiveShadows", asset.renderState.receiveShadows },
			{ "blendMode", EnumAdapter<BlendMode>::ToString(asset.renderState.blendMode) },
			{ "fillMode", EnumAdapter<D3D12_FILL_MODE>::ToString(asset.renderState.fillMode) },
			{ "cullMode", EnumAdapter<D3D12_CULL_MODE>::ToString(asset.renderState.cullMode) },
			{ "frontCounterClockwise", asset.renderState.frontCounterClockwise },
			{ "depthClipEnable", asset.renderState.depthClipEnable },
			{ "depthFunc", EnumAdapter<D3D12_COMPARISON_FUNC>::ToString(asset.renderState.depthFunc) },
			{ "stencilEnable", asset.renderState.stencilEnable },
			} },
		{ "outputNode", asset.outputNode ? ToString(asset.outputNode) : "" },
		{ "vertexOutputNode", asset.vertexOutputNode ? ToString(asset.vertexOutputNode) : "" },
		{ "parameters", nlohmann::json::array() },
		{ "keywords", nlohmann::json::array() },
		{ "nodes", nlohmann::json::array() },
		{ "groups", nlohmann::json::array() },
		{ "links", nlohmann::json::array() },
	};

	for (const ShaderGraphParameter& parameter : asset.parameters) {
		data["parameters"].push_back({
			{ "id", ToString(parameter.id) },
			{ "name", parameter.name },
			{ "type", EnumAdapter<ShaderGraphValueType>::ToString(parameter.type) },
			{ "semantic", EnumAdapter<MaterialParameterSemantic>::ToString(parameter.semantic) },
			{ "defaultValue", SerializeMaterialParameterValue(parameter.defaultValue) },
			{ "precision", EnumAdapter<ShaderGraphPrecision>::ToString(parameter.precision) },
			{ "scope", EnumAdapter<ShaderGraphParameterScope>::ToString(parameter.scope) },
			{ "exposed", parameter.exposed },
			{ "referenceName", parameter.referenceName },
			});
	}
	for (const ShaderGraphKeyword& keyword : asset.keywords) {
		data["keywords"].push_back({
			{ "id", ToString(keyword.id) },
			{ "name", keyword.name },
			{ "referenceName", keyword.referenceName },
			{ "type", EnumAdapter<ShaderGraphKeywordType>::ToString(keyword.type) },
			{ "entries", keyword.entries },
			{ "defaultIndex", keyword.defaultIndex },
			{ "runtimeToggle", keyword.runtimeToggle },
			});
	}
	for (const ShaderGraphNode& node : asset.nodes) {
		nlohmann::json nodeData{
			{ "id", ToString(node.id) },
			{ "groupID", node.groupID ? ToString(node.groupID) : "" },
			{ "kind", EnumAdapter<ShaderGraphNodeKind>::ToString(node.kind) },
			{ "parameterID", node.parameterID ? ToString(node.parameterID) : "" },
			{ "valueType", EnumAdapter<ShaderGraphValueType>::ToString(node.valueType) },
			{ "value", SerializeMaterialParameterValue(node.value) },
			{ "position", node.position.ToJson() },
			{ "stage", EnumAdapter<ShaderGraphStage>::ToString(node.stage) },
			{ "precision", EnumAdapter<ShaderGraphPrecision>::ToString(node.precision) },
			{ "inputPorts", WritePorts(node.inputPorts) },
			{ "outputPorts", WritePorts(node.outputPorts) },
			{ "subGraph", ToAssetReferenceJson(node.subGraph) },
			{ "keywordID", node.keywordID ? ToString(node.keywordID) : "" },
			{ "customFunctionSource", EnumAdapter<ShaderGraphCustomFunctionSource>::ToString(node.customFunctionSource) },
			{ "functionName", node.functionName },
			{ "functionFileAsset", ToAssetReferenceJson(node.functionFileAsset) },
			{ "functionFile", node.functionFile },
			{ "functionBody", node.functionBody },
			{ "previewExpanded", node.previewExpanded },
		};
		if (node.kind == ShaderGraphNodeKind::SamplerState) {
			nodeData["sampler"] = WriteSampler(node.sampler);
		}
		data["nodes"].push_back(std::move(nodeData));
	}
	for (const ShaderGraphGroup& group : asset.groups) {
		data["groups"].push_back({
			{ "id", ToString(group.id) },
			{ "name", group.name },
			{ "position", group.position.ToJson() },
			{ "size", group.size.ToJson() },
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
