#include "ShaderGraphAsset.h"

//============================================================================
//	include
//============================================================================
// c++
#include <utility>

namespace {

	using namespace Engine;

	// 数値の初期値を作る
	MaterialParameterValue FloatValue(float value) {

		MaterialParameterValue result{};
		result.value = value;
		return result;
	}

	// 色の初期値を作る
	MaterialParameterValue ColorValue(const Color4& value) {

		MaterialParameterValue result{};
		result.value = value;
		return result;
	}

	// 定数Nodeへ新しいIDを割り当てる
	ShaderGraphNode MakeConstantNode(ShaderGraphValueType type, MaterialParameterValue value, Vector2 position) {

		return ShaderGraphNode{
			.id = Engine::UUID::New(),
			.kind = ShaderGraphNodeKind::Constant,
			.valueType = type,
			.value = std::move(value),
			.position = position,
			.previewExpanded = false,
		};
	}
}

Engine::ShaderGraphAsset Engine::CreateDefaultSurfaceShaderGraph(std::string_view name, ShaderGraphTarget target) {

	ShaderGraphAsset graph{};
	graph.name = name.empty() ? "NewShaderGraph" : std::string(name);
	graph.domain = ShaderGraphDomain::Surface;
	graph.target = target;
	if (target == ShaderGraphTarget::Particle || target == ShaderGraphTarget::Trail) {
		graph.surfaceMode = ShaderGraphSurfaceMode::Transparent;
		graph.renderState.depthWrite = false;
		graph.renderState.cullMode = D3D12_CULL_MODE_NONE;
	}

	ShaderGraphNode output{
		.id = Engine::UUID::New(),
		.kind = IsShaderGraph3DTarget(target) ? ShaderGraphNodeKind::SurfaceOutput : ShaderGraphNodeKind::UnlitOutput,
		.position = Vector2(520.0f, 120.0f),
	};
	graph.outputNode = output.id;
	graph.nodes.emplace_back(output);

	auto addLink = [&](UUID source, uint32_t sourceSlot, UUID destination, uint32_t destinationSlot) {
		graph.links.emplace_back(ShaderGraphLink{
			.id = Engine::UUID::New(),
			.outputNode = source,
			.outputSlot = sourceSlot,
			.inputNode = destination,
			.inputSlot = destinationSlot,
		});
	};
	auto addConstant = [&](ShaderGraphValueType type, MaterialParameterValue value, Vector2 position) {
		ShaderGraphNode node = MakeConstantNode(type, std::move(value), position);
		const UUID id = node.id;
		graph.nodes.emplace_back(std::move(node));
		return id;
	};

	const UUID baseColor = addConstant(ShaderGraphValueType::Color, ColorValue(Color4::White()), Vector2(40.0f, 20.0f));
	addLink(baseColor, 0, output.id, 0);

	if (IsShaderGraph3DTarget(target)) {
		// 法線は未接続にし、VS/MSが出力した幾何法線を使用する
		const UUID metallic = addConstant(ShaderGraphValueType::Float, FloatValue(0.0f), Vector2(40.0f, 140.0f));
		const UUID roughness = addConstant(ShaderGraphValueType::Float, FloatValue(0.5f), Vector2(40.0f, 230.0f));
		const UUID ambientOcclusion = addConstant(ShaderGraphValueType::Float, FloatValue(1.0f), Vector2(40.0f, 320.0f));
		const UUID emissive =
			addConstant(ShaderGraphValueType::Color, ColorValue(Color4(0.0f, 0.0f, 0.0f, 1.0f)), Vector2(40.0f, 410.0f));
		const UUID opacity = addConstant(ShaderGraphValueType::Float, FloatValue(1.0f), Vector2(40.0f, 530.0f));
		const UUID alphaClip = addConstant(ShaderGraphValueType::Float, FloatValue(0.0f), Vector2(40.0f, 620.0f));
		addLink(metallic, 0, output.id, 2);
		addLink(roughness, 0, output.id, 3);
		addLink(ambientOcclusion, 0, output.id, 4);
		addLink(emissive, 0, output.id, 5);
		addLink(opacity, 0, output.id, 6);
		addLink(alphaClip, 0, output.id, 7);
	} else {
		const UUID opacity = addConstant(ShaderGraphValueType::Float, FloatValue(1.0f), Vector2(40.0f, 140.0f));
		const UUID alphaClip = addConstant(ShaderGraphValueType::Float, FloatValue(0.0f), Vector2(40.0f, 230.0f));
		addLink(opacity, 0, output.id, 1);
		addLink(alphaClip, 0, output.id, 2);
	}
	return graph;
}

Engine::ShaderGraphAsset Engine::CreateDefaultPostProcessShaderGraph(std::string_view name) {

	ShaderGraphAsset graph{};
	graph.name = name.empty() ? "NewPostProcessGraph" : std::string(name);
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

Engine::ShaderGraphAsset Engine::CreateDefaultRayTracingEffectShaderGraph(std::string_view name) {

	ShaderGraphAsset graph{};
	graph.name = name.empty() ? "NewRayTracingEffectGraph" : std::string(name);
	graph.domain = ShaderGraphDomain::RayTracingEffect;

	ShaderGraphNode output{
		.id = Engine::UUID::New(),
		.kind = ShaderGraphNodeKind::RayTracingOutput,
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
