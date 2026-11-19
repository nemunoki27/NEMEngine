#include "ShaderGraphNodePreviewCases.h"
#include "ShaderGraphNodePreviewGPUFixture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAssetReferences.h>

// c++
#include <array>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>

using namespace Engine;

bool NEMTests::TestShaderGraphNodePreviewDependencies(ShaderGraphNodePreviewGPUFixture& fixture) {

	auto time = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Time);
	const std::array values{1.25f, std::sin(1.25f), std::cos(1.25f), .02f, .03f};
	for (uint32_t slot = 0; slot < values.size(); ++slot) {
		const auto output = ShaderGraphNodePreviewCompiler::Compile(time.graph, time.nodeID, slot, {.rawOutput = true});
		if (!output.usesTime || !ExpectShaderGraphPreview(fixture, time, {values[slot], values[slot], values[slot], 1}, slot)) return false;
	}
	if (!ExpectShaderGraphPreview(fixture, time, {2.5f, 2.5f, 2.5f, 1}, 0, 2.5f)) return false;

	for (const auto kind : {ShaderGraphNodeKind::SceneColor, ShaderGraphNodeKind::SceneEmissive, ShaderGraphNodeKind::SceneMaterial}) {
		auto scene = CreateShaderGraphNodePreviewCase(kind);
		const auto output = ShaderGraphNodePreviewCompiler::Compile(scene.graph, scene.nodeID);
		if (!output.usesScene || !ExpectShaderGraphPreview(fixture, scene, {.25f, .5f, .75f, 1})) return false;
	}
	auto depth = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::SceneDepth);
	if (!ExpectShaderGraphPreview(fixture, depth, {.25f, .25f, .25f, 1})) return false;
	auto normal = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::SceneNormal);
	if (!ExpectShaderGraphPreview(fixture, normal, {-std::sqrt(.5f), 0, std::sqrt(.5f), 1})) return false;
	auto flags = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::SceneFlags);
	ConnectShaderGraphPreviewValue(flags, 0, ShaderGraphValueType::Float2, {Vector2(.75f, .25f)});
	if (!ExpectShaderGraphPreview(fixture, flags, {216, 216, 216, 1})) return false;
	flags.graph.domain = ShaderGraphDomain::PostProcess;
	if (!ExpectShaderGraphPreview(fixture, flags, {216, 216, 216, 1})) return false;
	auto outsideFlags = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::SceneFlags);
	ConnectShaderGraphPreviewValue(outsideFlags, 0, ShaderGraphValueType::Float2, {Vector2(1.5f, -.5f)});
	if (!ExpectShaderGraphPreview(fixture, outsideFlags, {31, 31, 31, 1})) return false;
	auto defaultFlags = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::SceneFlags);
	const auto flagImage = fixture.Render(ShaderGraphNodePreviewCompiler::Compile(defaultFlags.graph, defaultFlags.nodeID,
		0, {.rawOutput = true}));
	for (uint32_t y = 0; y < fixture.GetSize(); ++y) {
		for (uint32_t x = 0; x < fixture.GetSize(); ++x) {
			const float expected = float(uint32_t((float(x) + .5f) / fixture.GetSize() * 32) +
				uint32_t((float(y) + .5f) / fixture.GetSize() * 24) * 32);
			const auto& pixel = flagImage[y * fixture.GetSize() + x];
			if (pixel.x != expected || pixel.y != expected || pixel.z != expected || pixel.w != 1) return false;
		}
	}

	// Keywordの静的値とMaterial値を両方評価する
	for (const bool runtime : {false, true}) {
		for (uint32_t index = 0; index < 2; ++index) {
			auto keyword = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Keyword);
			keyword.graph.keywords.front().runtimeToggle = runtime;
			keyword.graph.keywords.front().defaultIndex = index;
			if (!ExpectShaderGraphPreview(fixture, keyword, {float(index), float(index), float(index), 1})) return false;
		}
	}

	// 出力ごとの型と関数の変更を評価する
	auto function = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::CustomFunction);
	auto& node = function.graph.nodes.front();
	node.functionName = "PreviewPair";
	node.outputPorts[0].type = ShaderGraphValueType::Float2;
	node.outputPorts.emplace_back(ShaderGraphPort{.id = Engine::UUID::New(), .name = "Alpha", .type = ShaderGraphValueType::Float});
	node.functionBody = "void PreviewPair(float4 In, out float2 Out, out float Alpha) { Out = In.rg * 2; Alpha = In.a; }";
	const auto first = ShaderGraphNodePreviewCompiler::Compile(function.graph, function.nodeID);
	if (!ExpectShaderGraphPreview(fixture, function, {.4f, .8f, 0, 1}) ||
		!ExpectShaderGraphPreview(fixture, function, {.75f, .75f, .75f, 1}, 1)) return false;
	node.functionBody = "void PreviewPair(float4 In, out float2 Out, out float Alpha) { Out = In.rg * 3; Alpha = In.a; }";
	const auto changed = ShaderGraphNodePreviewCompiler::Compile(function.graph, function.nodeID);
	if (first.pixelHLSL == changed.pixelHLSL || !ExpectShaderGraphPreview(fixture, function, {.6f, 1.2f, 0, 1})) return false;

	// File形式も同じ関数宣言経路を使う
	TestDirectory directory("ShaderGraphPreviewFunction");
	const auto filePath = directory.GetPath() / "PreviewPair.hlsli";
	std::ofstream file(filePath);
	file << node.functionBody;
	file.close();
	node.customFunctionSource = ShaderGraphCustomFunctionSource::File;
	node.functionFile = filePath.generic_string();
	if (!ExpectShaderGraphPreview(fixture, function, {.6f, 1.2f, 0, 1})) return false;

	auto subGraph = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::SubGraph);
	if (!ExpectShaderGraphPreview(fixture, subGraph, {1, 1, 1, 1}, 0) ||
		!ExpectShaderGraphPreview(fixture, subGraph, {0, 0, 1, 1}, 1) ||
		!ExpectShaderGraphPreview(fixture, subGraph, {.5f, .5f, .5f, 1}, 3) ||
		!ExpectShaderGraphPreview(fixture, subGraph, {0, 0, 0, 1}, 8) ||
		!ExpectShaderGraphPreview(fixture, subGraph, {1, 1, 1, 1}, 9)) return false;
	const auto child = std::make_shared<ShaderGraphAsset>(CreateShaderGraphPreviewSubGraph());
	for (auto& source : child->nodes) {
		if (source.kind == ShaderGraphNodeKind::Constant && source.valueType == ShaderGraphValueType::Float &&
			std::get<float>(source.value.value) == .5f) source.value.value = .25f;
	}
	subGraph.resolver = [child](AssetID id, ShaderGraphAsset& resolved) {
		if (id != AssetID{71, 10}) return false;
		resolved = *child;
		return true;
	};
	if (!ExpectShaderGraphPreview(fixture, subGraph, {.25f, .25f, .25f, 1}, 3)) return false;

	// 同じ関数を複数配置しても宣言を重複させない
	auto combined = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Add);
	for (uint32_t slot = 0; slot < 2; ++slot) {
		const auto source = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::CustomFunction).graph.nodes.front();
		combined.graph.nodes.emplace_back(source);
		combined.graph.links.emplace_back(ShaderGraphLink{
			.id = Engine::UUID::New(), .outputNode = source.id, .inputNode = combined.nodeID, .inputSlot = slot,
		});
	}
	if (!ExpectShaderGraphPreview(fixture, combined, {.4f, .8f, 1.2f, 1.5f})) return false;

	auto repeated = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Add);
	const auto functionChild = std::make_shared<ShaderGraphAsset>(CreateShaderGraphPreviewSubGraph());
	const auto functionSource = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::CustomFunction).graph.nodes.front();
	functionChild->nodes.emplace_back(functionSource);
	std::erase_if(functionChild->links, [&](const ShaderGraphLink& link) {
		return link.inputNode == functionChild->outputNode && link.inputSlot == 0;
	});
	functionChild->links.emplace_back(ShaderGraphLink{
		.id = Engine::UUID::New(), .outputNode = functionSource.id, .inputNode = functionChild->outputNode,
	});
	for (uint32_t slot = 0; slot < 2; ++slot) {
		const auto source = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::SubGraph).graph.nodes.front();
		repeated.graph.nodes.emplace_back(source);
		repeated.graph.links.emplace_back(ShaderGraphLink{
			.id = Engine::UUID::New(), .outputNode = source.id, .inputNode = repeated.nodeID, .inputSlot = slot,
		});
	}
	repeated.resolver = [functionChild](AssetID id, ShaderGraphAsset& resolved) {
		if (id != AssetID{71, 10}) return false;
		resolved = *functionChild;
		return true;
	};
	if (!ExpectShaderGraphPreview(fixture, repeated, {.4f, .8f, 1.2f, 1.5f})) return false;

	// 子のTexture既定値を非公開でも失わず親の接続を優先する
	for (uint32_t mode = 0; mode < 3; ++mode) {
		const auto textureCase = CreateShaderGraphNodePreviewTextureCase({.25f, .25f},
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_FILTER_MIN_MAG_MIP_POINT);
		const auto textureChild = std::make_shared<ShaderGraphAsset>(CreateShaderGraphPreviewSubGraph());
		textureChild->parameters = textureCase.graph.parameters;
		textureChild->parameters.front().exposed = mode != 0;
		textureChild->parameters.front().semantic = MaterialParameterSemantic::BaseColorTexture;
		textureChild->nodes.insert(textureChild->nodes.end(), textureCase.graph.nodes.begin(), textureCase.graph.nodes.end());
		textureChild->links.insert(textureChild->links.end(), textureCase.graph.links.begin(), textureCase.graph.links.end());
		std::erase_if(textureChild->links, [&](const ShaderGraphLink& link) {
			return link.inputNode == textureChild->outputNode && link.inputSlot == 0;
		});
		textureChild->links.emplace_back(ShaderGraphLink{
			.id = Engine::UUID::New(), .outputNode = textureCase.nodeID, .inputNode = textureChild->outputNode,
		});
		auto parent = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::SubGraph);
		ShaderGraphAssetReferences::RefreshSubGraphPorts(parent.graph.nodes.front(), *textureChild);
		if (!parent.graph.nodes.front().inputPorts.empty()) {
			parent.graph.nodes.front().inputPorts.front().defaultValue.value = AssetID{71, 9999};
		}
		if (mode == 2) {
			textureChild->parameters.front().defaultValue.value = AssetID{71, 3333};
			ConnectShaderGraphPreviewValue(parent, 0, ShaderGraphValueType::Texture2D, {fixture.GetTextureID()});
		}
		parent.resolver = [textureChild](AssetID id, ShaderGraphAsset& resolved) {
			if (id != AssetID{71, 10}) return false;
			resolved = *textureChild;
			return true;
		};
		auto nativeGraph = CreateDefaultSurfaceShaderGraph("SubGraphTexture");
		std::erase_if(nativeGraph.links, [&](const ShaderGraphLink& link) {
			return link.inputNode == nativeGraph.outputNode && link.inputSlot == 0;
		});
		nativeGraph.nodes.insert(nativeGraph.nodes.end(), parent.graph.nodes.begin(), parent.graph.nodes.end());
		nativeGraph.links.insert(nativeGraph.links.end(), parent.graph.links.begin(), parent.graph.links.end());
		nativeGraph.links.emplace_back(ShaderGraphLink{
			.id = Engine::UUID::New(), .outputNode = parent.nodeID, .inputNode = nativeGraph.outputNode,
		});
		const auto compiled = ShaderGraphCompiler::Compile(nativeGraph, "surface.hlsli", parent.resolver);
		const auto parameter = std::find_if(compiled.parameters.begin(), compiled.parameters.end(), [](const auto& metadata) {
			return metadata.isTexture;
		});
		if (!compiled.Succeeded() || parameter == compiled.parameters.end() ||
			(mode != 2 && parameter->semantic != MaterialParameterSemantic::BaseColorTexture)) return false;
		const auto* initial = compiled.defaultParameters.Find(parameter->id);
		if (!initial || !std::holds_alternative<AssetID>(initial->value) ||
			std::get<AssetID>(initial->value) != fixture.GetTextureID()) return false;
		if (!ExpectShaderGraphPreview(fixture, parent, {1, 0, 0, .25f})) return false;
	}
	return fixture.CheckMessages();
}
