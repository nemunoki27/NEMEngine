#include "ShaderGraphNodePreviewCases.h"
#include "ShaderGraphNodePreviewGPUFixture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>

// c++
#include <array>
#include <cmath>
#include <iostream>
#include <memory>

using namespace Engine;

NEMTests::ShaderGraphNodePreviewCase NEMTests::CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind kind) {

	ShaderGraphNodePreviewCase result;
	result.graph.name = std::string(GetShaderGraphNodeName(kind));
	result.graph.surfaceMode = ShaderGraphSurfaceMode::Transparent;
	result.nodeID = Engine::UUID::New();
	result.graph.outputNode = result.nodeID;
	ShaderGraphNode node{.id = result.nodeID, .kind = kind};
	if (kind == ShaderGraphNodeKind::Constant) {
		node.valueType = ShaderGraphValueType::Float4;
		node.value = {Vector4(.2f, .4f, .6f, .75f)};
	} else if (kind == ShaderGraphNodeKind::Parameter) {
		node.parameterID = Engine::UUID::New();
		result.graph.parameters.emplace_back(ShaderGraphParameter{
			.id = node.parameterID, .name = "PreviewValue", .type = ShaderGraphValueType::Float4,
			.defaultValue = {Vector4(.2f, .4f, .6f, .75f)}, .exposed = false,
		});
	} else if (kind == ShaderGraphNodeKind::Keyword) {
		node.keywordID = Engine::UUID::New();
		result.graph.keywords.emplace_back(ShaderGraphKeyword{
			.id = node.keywordID, .name = "PreviewKeyword", .defaultIndex = 1, .runtimeToggle = true,
		});
	} else if (kind == ShaderGraphNodeKind::CustomFunction) {
		node.functionName = "PreviewIdentity";
		node.functionBody = "void PreviewIdentity(float4 In, out float4 Out) { Out = In; }";
		node.inputPorts.emplace_back(ShaderGraphPort{
			.id = Engine::UUID::New(), .name = "In", .type = ShaderGraphValueType::Float4,
			.defaultValue = {Vector4(.2f, .4f, .6f, .75f)},
		});
		node.outputPorts.emplace_back(ShaderGraphPort{
			.id = Engine::UUID::New(), .name = "Out", .type = ShaderGraphValueType::Float4,
		});
	} else if (kind == ShaderGraphNodeKind::SubGraph) {
		node.subGraph = {71, 10};
		const auto child = std::make_shared<ShaderGraphAsset>(CreateShaderGraphPreviewSubGraph());
		const auto* descriptor = ShaderGraphNodeRegistry::Find(ShaderGraphNodeKind::SurfaceOutput);
		for (const auto& input : descriptor->inputs) {
			node.outputPorts.emplace_back(ShaderGraphPort{.id = Engine::UUID::New(), .name = std::string(input.name), .type = input.type});
		}
		result.resolver = [child](AssetID id, ShaderGraphAsset& resolved) {
			if (id != AssetID{71, 10}) return false;
			resolved = *child;
			return true;
		};
	} else if (kind == ShaderGraphNodeKind::TextureSample) {
		node.value = {Vector4(.2f, .4f, .6f, .75f)};
	} else if (kind == ShaderGraphNodeKind::ParticleData) {
		result.graph.target = ShaderGraphTarget::Particle;
	}
	result.graph.nodes.emplace_back(std::move(node));
	return result;
}

NEMTests::ShaderGraphNodePreviewCase NEMTests::CreateShaderGraphNodePreviewTextureCase(Vector2 uv,
	D3D12_TEXTURE_ADDRESS_MODE mode, D3D12_FILTER filter) {

	auto testCase = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::TextureSample);
	const Engine::UUID parameterID = Engine::UUID::New(), textureNode = Engine::UUID::New(), samplerNode = Engine::UUID::New();
	testCase.graph.parameters.emplace_back(ShaderGraphParameter{
		.id = parameterID, .name = "PreviewTexture", .type = ShaderGraphValueType::Texture2D,
		.defaultValue = {ShaderGraphNodePreviewGPUFixture::GetTextureID()},
	});
	testCase.graph.nodes.emplace_back(ShaderGraphNode{
		.id = textureNode, .kind = ShaderGraphNodeKind::Parameter, .parameterID = parameterID,
	});
	ShaderGraphNode sampler{.id = samplerNode, .kind = ShaderGraphNodeKind::SamplerState};
	sampler.sampler.filter = filter;
	sampler.sampler.addressU = mode;
	sampler.sampler.addressV = mode;
	testCase.graph.nodes.emplace_back(sampler);
	testCase.graph.links.emplace_back(ShaderGraphLink{
		.id = Engine::UUID::New(), .outputNode = textureNode, .inputNode = testCase.nodeID, .inputSlot = 0,
	});
	testCase.graph.links.emplace_back(ShaderGraphLink{
		.id = Engine::UUID::New(), .outputNode = samplerNode, .inputNode = testCase.nodeID, .inputSlot = 2,
	});
	ConnectShaderGraphPreviewValue(testCase, 1, ShaderGraphValueType::Float2, {uv});
	return testCase;
}

void NEMTests::ConnectShaderGraphPreviewValue(ShaderGraphNodePreviewCase& testCase, uint32_t inputSlot,
	ShaderGraphValueType type, const MaterialParameterValue& value) {

	const Engine::UUID id = Engine::UUID::New();
	testCase.graph.nodes.emplace_back(ShaderGraphNode{.id = id, .kind = ShaderGraphNodeKind::Constant,
		.valueType = type, .value = value, .position = {-400, float(inputSlot) * 120}, .previewExpanded = false});
	testCase.graph.links.emplace_back(ShaderGraphLink{
		.id = Engine::UUID::New(), .outputNode = id, .inputNode = testCase.nodeID, .inputSlot = inputSlot,
	});
}

bool NEMTests::ExpectShaderGraphPreview(ShaderGraphNodePreviewGPUFixture& fixture,
	const ShaderGraphNodePreviewCase& testCase, const Vector4& expected, uint32_t slot, float time) {

	const auto compiled = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID,
		slot, {.rawOutput = true}, testCase.resolver);
	const auto image = fixture.Render(compiled, time);
	for (const Vector4& pixel : image) {
		const std::array actual{pixel.x, pixel.y, pixel.z, pixel.w};
		const std::array wanted{expected.x, expected.y, expected.z, expected.w};
		for (size_t channel = 0; channel < actual.size(); ++channel) {
			if (!std::isfinite(actual[channel]) || .00005f < std::abs(actual[channel] - wanted[channel])) {
				std::cerr << "Node value mismatch: " << testCase.graph.name << " slot=" << slot <<
					" channel=" << channel << " actual=" << actual[channel] << " expected=" << wanted[channel] << '\n';
				return false;
			}
		}
	}
	return true;
}

ShaderGraphAsset NEMTests::CreateShaderGraphPreviewSubGraph() {

	auto child = CreateDefaultSurfaceShaderGraph("PreviewSubGraph");
	child.nodes.front().previewExpanded = false;
	const auto addOutput = [&](uint32_t slot, ShaderGraphValueType type, const MaterialParameterValue& value) {
		const Engine::UUID id = Engine::UUID::New();
		child.nodes.emplace_back(ShaderGraphNode{
			.id = id, .kind = ShaderGraphNodeKind::Constant, .valueType = type,
			.value = value, .previewExpanded = false,
		});
		child.links.emplace_back(ShaderGraphLink{
			.id = Engine::UUID::New(), .outputNode = id, .inputNode = child.outputNode, .inputSlot = slot,
		});
	};
	// 子Graphの公開出力は未接続の既定値へ依存せず明示する
	addOutput(1, ShaderGraphValueType::Float3, {Vector3(0, 0, 1)});
	addOutput(8, ShaderGraphValueType::Float, {0.0f});
	addOutput(9, ShaderGraphValueType::Float3, {Vector3(1, 1, 1)});
	return child;
}
