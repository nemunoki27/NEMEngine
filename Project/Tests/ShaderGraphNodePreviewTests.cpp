#include "ShaderGraphNodePreviewCases.h"
#include "ShaderGraphNodePreviewGPUFixture.h"
#include "TestRunner.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphFrameConstants.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAssetReferences.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// c++
#include <algorithm>
#include <iostream>
#include <memory>
#include <cmath>
#include <limits>
#include <unordered_set>

using namespace Engine;

namespace {

	// Viewと逆行列を別の描画領域へ取り違えないか確認する
	bool TestFrameConstants() {

		SystemContext system;
		system.time = 2;
		system.deltaTime = .02f;
		system.smoothDeltaTime = .03f;
		system.unscaledTime = 3;
		ResolvedRenderView view;
		view.width = 640;
		view.height = 480;
		view.referenceWidth = 1280;
		view.referenceHeight = 960;
		auto& camera = view.perspective;
		camera.valid = true;
		camera.cameraPos = {1, 2, 3};
		camera.forward = {0, 0, -1};
		camera.nearClip = .2f;
		camera.farClip = 500;
		camera.matrices.viewMatrix = Matrix4x4::MakeTranslateMatrix({-1, -2, -3});
		camera.matrices.inverseViewMatrix = Matrix4x4::MakeTranslateMatrix({1, 2, 3});
		camera.matrices.projectionMatrix = Matrix4x4::MakeScaleMatrix({2, 4, .5f});
		camera.matrices.inverseProjectionMatrix = Matrix4x4::MakeScaleMatrix({.5f, .25f, 2});
		camera.matrices.viewProjectionMatrix = camera.matrices.viewMatrix * camera.matrices.projectionMatrix;
		const auto constants = BuildShaderGraphFrameConstants(system, view);
		if (constants.time != 2 || constants.deltaTime != .02f || constants.smoothDeltaTime != .03f ||
			constants.unscaledTime != 3 || constants.screenSize.x != 640 || constants.screenSize.y != 480 ||
			constants.viewport != Vector4(0, 0, 640, 480) || sizeof(constants) != 480 ||
			constants.cameraPosition.x != 1 || constants.cameraPosition.y != 2 || constants.cameraPosition.z != 3 ||
			constants.cameraDirection.z != -1 || constants.cameraProjection.x != 0 ||
			constants.cameraProjection.y != .2f || constants.cameraProjection.z != 500) return false;
		const auto identity = constants.worldToClip * constants.clipToWorld;
		for (uint32_t row = 0; row < 4; ++row) {
			for (uint32_t column = 0; column < 4; ++column) {
				if (.00001f < std::abs(identity.m[row][column] - (row == column ? 1.0f : 0.0f))) return false;
			}
		}
		const auto atlas = BuildShaderGraphFrameConstants(system, view, RenderCameraDomain::Perspective, {32, 48, 320, 240});
		if (atlas.viewport != Vector4(32, 48, 320, 240) || atlas.screenSize.x != 320 || atlas.screenSize.y != 240 ||
			atlas.clipToWorld != constants.clipToWorld) return false;
		view.kind = RenderViewKind::Scene;
		view.orthographic = camera;
		view.orthographic.projectionMode = ResolvedProjectionMode::Orthographic;
		const auto orthographic = BuildShaderGraphFrameConstants(system, view, RenderCameraDomain::Screen);
		return orthographic.cameraProjection.x == 1 && orthographic.screenSize.z == 1 && orthographic.screenSize.w == .5f;
	}

	// 数値のドラッグでShaderを作り直さず転送値だけを更新する
	bool TestUniformEdits() {

		using namespace NEMTests;
		const auto check = [](ShaderGraphNodePreviewCase testCase, const auto& edit) {
			const auto original = ToJson(testCase.graph);
			const auto first = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID);
			if (ToJson(testCase.graph) != original) return false;
			edit(testCase.graph.nodes.front());
			const auto changed = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID);
			return first.Succeeded() && changed.Succeeded() && first.pixelHLSL == changed.pixelHLSL &&
				BuildShaderGraphNodePreviewPipelineKey(first) == BuildShaderGraphNodePreviewPipelineKey(changed) &&
				MaterialParameterBufferBuilder::ComputeHash(first.defaultParameters) !=
				MaterialParameterBufferBuilder::ComputeHash(changed.defaultParameters);
		};
		for (const auto kind : {ShaderGraphNodeKind::Matrix2, ShaderGraphNodeKind::Matrix3,
			ShaderGraphNodeKind::Matrix4, ShaderGraphNodeKind::MatrixConstruction}) {
			if (!check(CreateShaderGraphNodePreviewCase(kind), [](ShaderGraphNode& node) {
				node.settings.matrixRows[0].x = 2;
			})) return false;
		}
		if (!check(CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Matrix4), [](ShaderGraphNode& node) {
			node.settings.matrixAxis = ShaderGraphMatrixAxis::Column;
		})) return false;
		auto gradient = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Gradient);
		gradient.graph.nodes.front().settings.gradientColors = {{0, 0, 0, 0}, {1, 1, 1, 1}};
		gradient.graph.nodes.front().settings.gradientAlphas = {{1, 0}, {1, 1}};
		if (!check(gradient, [](ShaderGraphNode& node) {
			node.settings.gradientColors[0].x = .25f;
			node.settings.gradientAlphas[0].x = .5f;
		})) return false;
		if (!check(gradient, [](ShaderGraphNode& node) {
			node.settings.gradientColors.emplace_back(Vector4(1, 0, 0, .5f));
			node.settings.gradientMode = ShaderGraphGradientMode::Fixed;
		})) return false;
		if (!check(CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::CustomFunction), [](ShaderGraphNode& node) {
			node.inputPorts.front().defaultValue.value = Vector4(.9f, .8f, .7f, .6f);
		})) return false;
		for (const auto kind : {ShaderGraphNodeKind::Contrast, ShaderGraphNodeKind::Saturation}) {
			if (!check(CreateShaderGraphNodePreviewCase(kind), [](ShaderGraphNode& node) {
				node.inputPorts.resize(2);
				for (auto& port : node.inputPorts) port.hasDefault = false;
				node.inputPorts[1].type = ShaderGraphValueType::Float;
				node.inputPorts[1].hasDefault = true;
				node.inputPorts[1].defaultValue.value = 2.0f;
			})) return false;
		}
		auto coordinates = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Spherize);
		coordinates.graph.nodes.front().inputPorts.resize(GetShaderGraphInputCount(ShaderGraphNodeKind::Spherize));
		for (auto& port : coordinates.graph.nodes.front().inputPorts) port.hasDefault = false;
		auto& uv = coordinates.graph.nodes.front().inputPorts.front();
		uv.type = ShaderGraphValueType::Float2;
		uv.hasDefault = true;
		uv.defaultValue.value = Vector2(.25f, .5f);
		return check(coordinates, [](ShaderGraphNode& node) {
			node.inputPorts.front().defaultValue.value = Vector2(.5f, .75f);
		});
	}

	// 旧形式の空ピンと子の型変更を安定した識別子へ接続する
	bool TestSubGraphInterfaces() {

		auto child = CreateDefaultSurfaceShaderGraph("Ports");
		const Engine::UUID firstID = Engine::UUID::New(), secondID = Engine::UUID::New();
		child.parameters.emplace_back(ShaderGraphParameter{.id = firstID, .name = "First"});
		child.parameters.emplace_back(ShaderGraphParameter{.id = secondID, .name = "Second", .type = ShaderGraphValueType::Float3});
		const Engine::UUID vectorID = Engine::UUID::New(), integerID = Engine::UUID::New();
		child.nodes.emplace_back(ShaderGraphNode{.id = vectorID, .kind = ShaderGraphNodeKind::Constant,
			.valueType = ShaderGraphValueType::Float2, .value = {Vector2(.25f, .75f)}});
		child.nodes.emplace_back(ShaderGraphNode{.id = integerID, .kind = ShaderGraphNodeKind::Constant,
			.valueType = ShaderGraphValueType::Integer, .value = {int32_t(2)}});
		std::erase_if(child.links, [&](const ShaderGraphLink& link) {
			return link.inputNode == child.outputNode && (link.inputSlot == 0 || link.inputSlot == 2);
		});
		child.links.emplace_back(ShaderGraphLink{.id = Engine::UUID::New(), .outputNode = vectorID, .inputNode = child.outputNode});
		child.links.emplace_back(ShaderGraphLink{
			.id = Engine::UUID::New(), .outputNode = integerID, .inputNode = child.outputNode, .inputSlot = 2,
		});
		ShaderGraphNode node{.id = Engine::UUID::New(), .kind = ShaderGraphNodeKind::SubGraph};
		if (!ShaderGraphAssetReferences::RefreshSubGraphPorts(node, child) || node.inputPorts.size() != 2 ||
			node.outputPorts.size() != GetShaderGraphInputCount(ShaderGraphNodeKind::SurfaceOutput) ||
			node.outputPorts[0].type != ShaderGraphValueType::Float2 ||
			node.outputPorts[2].type != ShaderGraphValueType::Integer) return false;
		const auto firstPort = node.inputPorts[0].id, secondPort = node.inputPorts[1].id, outputPort = node.outputPorts[0].id;
		if (ShaderGraphAssetReferences::RefreshSubGraphPorts(node, child)) return false;
		std::swap(child.parameters[0], child.parameters[1]);
		child.parameters[0].name = "RenamedSecond";
		if (!ShaderGraphAssetReferences::RefreshSubGraphPorts(node, child) ||
			node.inputPorts[0].id != secondPort || node.inputPorts[0].sourceID != secondID ||
			node.inputPorts[1].id != firstPort || node.inputPorts[1].sourceID != firstID ||
			node.outputPorts[0].id != outputPort) return false;
		auto parent = NEMTests::CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::SubGraph).graph;
		parent.nodes.front() = node;
		parent.outputNode = node.id;
		ShaderGraphAsset restored;
		return FromJson(ToJson(parent), restored) && restored.nodes.front().inputPorts[0].sourceID == secondID &&
			restored.nodes.front().inputPorts[1].id == firstPort;
	}
}

bool NEMTests::TestShaderGraphNodePreviewContracts() {

	if (!TestFrameConstants() || !TestUniformEdits() || !TestSubGraphInterfaces()) return false;
	auto testCase = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Constant);
	const auto baseline = ToJson(testCase.graph);
	const auto first = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID);
	if (ToJson(testCase.graph) != baseline) return false;
	auto& node = testCase.graph.nodes.front();
	node.value.value = Vector4(.8f, .4f, .6f, .25f);
	const auto edited = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID);
	if (!first.Succeeded() || first.pixelHLSL != edited.pixelHLSL ||
		MaterialParameterBufferBuilder::ComputeHash(first.defaultParameters) ==
		MaterialParameterBufferBuilder::ComputeHash(edited.defaultParameters)) return false;
	node.previewMode = ShaderGraphPreviewMode::ThreeDimensional;
	node.previewOutputSlot = 0;
	ShaderGraphAsset restored;
	if (!FromJson(ToJson(testCase.graph), restored) ||
		restored.nodes.front().previewMode != ShaderGraphPreviewMode::ThreeDimensional) return false;
	auto future = baseline;
	future["schemaVersion"] = 10;
	const auto beforeFailure = ToJson(restored);
	if (FromJson(future, restored) || ToJson(restored) != beforeFailure) return false;
	auto legacy = baseline;
	legacy["schemaVersion"] = 8;
	legacy["nodes"][0].erase("previewMode");
	legacy["nodes"][0].erase("previewOutputSlot");
	if (!FromJson(legacy, restored) || restored.nodes.front().previewMode != ShaderGraphPreviewMode::Inherit ||
		restored.nodes.front().previewOutputSlot != 0) return false;
	for (const auto mode : {ShaderGraphPreviewMode::Inherit, ShaderGraphPreviewMode::TwoDimensional,
		ShaderGraphPreviewMode::ThreeDimensional}) {
		auto saved = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::TextureSample);
		saved.graph.nodes.front().previewMode = mode;
		saved.graph.nodes.front().previewOutputSlot = 5;
		if (!FromJson(ToJson(saved.graph), restored) || restored.nodes.front().previewMode != mode ||
			restored.nodes.front().previewOutputSlot != 5) return false;
	}
	if (ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID, 100).Succeeded()) return false;
	if (ShaderGraphNodePreviewCompiler::Compile(testCase.graph, Engine::UUID::New()).Succeeded()) return false;
	for (uint32_t invalid = 0; invalid < 4; ++invalid) {
		auto samplerCase = CreateShaderGraphNodePreviewTextureCase({.25f, .25f},
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_FILTER_MIN_MAG_MIP_POINT);
		auto sampler = std::find_if(samplerCase.graph.nodes.begin(), samplerCase.graph.nodes.end(), [](const auto& value) {
			return value.kind == ShaderGraphNodeKind::SamplerState;
		});
		if (invalid == 0) sampler->sampler.filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
		if (invalid == 1) { sampler->sampler.minLOD = 2; sampler->sampler.maxLOD = 1; }
		if (invalid == 2) sampler->sampler.mipLODBias = std::numeric_limits<float>::quiet_NaN();
		if (invalid == 3) sampler->sampler.mipLODBias = 17.0f;
		const auto rejected = ShaderGraphNodePreviewCompiler::Compile(samplerCase.graph, samplerCase.nodeID);
		if (rejected.Succeeded() || rejected.diagnostics.empty()) return false;
	}
	for (const auto type : {ShaderGraphValueType::Matrix2, ShaderGraphValueType::Matrix3,
		ShaderGraphValueType::Matrix4, ShaderGraphValueType::Gradient}) {
		auto parameter = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Parameter);
		parameter.graph.parameters.front().type = type;
		if (ShaderGraphNodePreviewCompiler::Compile(parameter.graph, parameter.nodeID).Succeeded()) return false;
	}

	// 参照されない壊れた関数を評価に混ぜない
	testCase.graph.nodes.emplace_back(ShaderGraphNode{
		.id = Engine::UUID::New(), .kind = ShaderGraphNodeKind::CustomFunction,
		.functionName = "Broken", .functionBody = "this is not valid HLSL",
	});
	const auto isolated = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID);
	if (!isolated.Succeeded() || isolated.pixelHLSL.find("this is not valid HLSL") != std::string::npos) return false;

	// 循環だけを拒否して他のプレビューを保つ
	auto cyclic = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Add);
	cyclic.graph.links.emplace_back(ShaderGraphLink{
		.id = Engine::UUID::New(), .outputNode = cyclic.nodeID, .inputNode = cyclic.nodeID,
	});
	if (ShaderGraphNodePreviewCompiler::Compile(cyclic.graph, cyclic.nodeID).Succeeded()) return false;
	const auto duplicate = testCase.graph.nodes.front();
	testCase.graph.nodes.emplace_back(duplicate);
	if (ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID).Succeeded()) return false;
	for (uint32_t invalid = 0; invalid < 3; ++invalid) {
		auto broken = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Add);
		ConnectShaderGraphPreviewValue(broken, 0, ShaderGraphValueType::Float, {.5f});
		if (invalid == 0) broken.graph.links.front().outputSlot = 99;
		if (invalid == 1) broken.graph.links.front().inputSlot = 99;
		if (invalid == 2) broken.graph.links.front().outputNode = Engine::UUID::New();
		if (ShaderGraphNodePreviewCompiler::Compile(broken.graph, broken.nodeID).Succeeded()) return false;
	}

	// 未接続の子出力を使用した場合だけ診断する
	auto disconnected = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::SubGraph);
	const auto incomplete = std::make_shared<ShaderGraphAsset>(CreateShaderGraphPreviewSubGraph());
	std::erase_if(incomplete->links, [&](const ShaderGraphLink& link) {
		return link.inputNode == incomplete->outputNode && link.inputSlot == 1;
	});
	disconnected.resolver = [incomplete](AssetID id, ShaderGraphAsset& resolved) {
		if (id != AssetID{71, 10}) return false;
		resolved = *incomplete;
		return true;
	};
	const auto missing = ShaderGraphNodePreviewCompiler::Compile(disconnected.graph, disconnected.nodeID,
		1, {}, disconnected.resolver);
	if (missing.Succeeded() || !std::any_of(missing.diagnostics.begin(), missing.diagnostics.end(), [](const auto& diagnostic) {
		return diagnostic.severity == ShaderGraphDiagnosticSeverity::Error &&
			diagnostic.message.find("未接続出力") != std::string::npos;
	}) || !ShaderGraphNodePreviewCompiler::Compile(disconnected.graph, disconnected.nodeID,
		0, {}, disconnected.resolver).Succeeded()) return false;

	// 子Graphでも参照されない出力のエラーを隔離する
	auto subGraph = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::SubGraph);
	const auto child = std::make_shared<ShaderGraphAsset>(CreateShaderGraphPreviewSubGraph());
	const Engine::UUID brokenID = Engine::UUID::New();
	child->nodes.emplace_back(ShaderGraphNode{
		.id = brokenID, .kind = ShaderGraphNodeKind::SubGraph, .subGraph = {71, 999},
	});
	std::erase_if(child->links, [&](const ShaderGraphLink& link) {
		return link.inputNode == child->outputNode && link.inputSlot == 2;
	});
	child->links.emplace_back(ShaderGraphLink{
		.id = Engine::UUID::New(), .outputNode = brokenID, .inputNode = child->outputNode, .inputSlot = 2,
	});
	subGraph.resolver = [child](AssetID id, ShaderGraphAsset& resolved) {
		if (id != AssetID{71, 10}) return false;
		resolved = *child;
		return true;
	};
	if (!ShaderGraphNodePreviewCompiler::Compile(subGraph.graph, subGraph.nodeID, 0, {}, subGraph.resolver).Succeeded() ||
		ShaderGraphNodePreviewCompiler::Compile(subGraph.graph, subGraph.nodeID, 2, {}, subGraph.resolver).Succeeded()) return false;
	return true;
}

bool NEMTests::TestShaderGraphNodePreviewCatalog(ShaderGraphNodePreviewGPUFixture& fixture) {

	std::unordered_set<uint32_t> visited;
	uint32_t outputCount = 0;
	for (const auto& descriptor : ShaderGraphNodeRegistry::GetDescriptors()) {
		if (!visited.insert(static_cast<uint32_t>(descriptor.kind)).second) return false;
		auto testCase = CreateShaderGraphNodePreviewCase(descriptor.kind);
		const auto generic = GetGenericShaderGraphNodeDescriptors();
		if (std::ranges::find(generic, descriptor.kind, &ShaderGraphNodeDescriptor::kind) != generic.end()) {
			for (uint32_t slot = 0; slot < descriptor.inputs.size(); ++slot) {
				if (GetShaderGraphInputDefault(testCase.graph.nodes.front(), slot).type == ShaderGraphValueType::Invalid) {
					std::cerr << "Node input default missing: " << descriptor.name << " input=" << slot << '\n';
					return false;
				}
			}
		}
		ShaderGraphAsset restored;
		if (!FromJson(ToJson(testCase.graph), restored) || restored.nodes.size() != testCase.graph.nodes.size() ||
			restored.nodes.front().kind != descriptor.kind) {
			std::cerr << "Node serialization failed: " << descriptor.name << '\n';
			return false;
		}
		if (!descriptor.preview) continue;
		const uint32_t count = GetShaderGraphOutputCount(testCase.graph.nodes.front());
		for (uint32_t slot = 0; slot < count; ++slot) {
			const auto output = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID,
				slot, {.rawOutput = true}, testCase.resolver);
			if (!output.Succeeded()) {
				std::cerr << "Node preview compilation failed: " << descriptor.name << " output=" << slot << '\n';
				for (const auto& diagnostic : output.diagnostics) std::cerr << diagnostic.message << '\n';
				return false;
			}
			try {
				const auto image = fixture.Render(output);
				if (image.size() != fixture.GetSize() * fixture.GetSize() ||
					std::all_of(image.begin(), image.end(), [](const Vector4& pixel) { return pixel.x == -999; })) return false;
			} catch (const std::exception& error) {
				std::cerr << "Node preview GPU failed: " << descriptor.name << " output=" << slot << ' ' << error.what() << '\n';
				return false;
			}
			++outputCount;
		}
	}
	std::cout << "Node preview catalog: " << visited.size() << " kinds, " << outputCount << " GPU outputs\n";
	return fixture.CheckMessages();
}

bool NEMTests::TestShaderGraphNodePreviews() {

	if (!RunTest("ShaderGraphNodePreviewContracts", TestShaderGraphNodePreviewContracts)) return false;
	if (!RunTest("ShaderGraphNodePreviewCache", TestShaderGraphNodePreviewCache)) return false;
	if (!RunTest("ShaderGraphNodePreviewTypes", TestShaderGraphNodePreviewTypes)) return false;
	ShaderGraphNodePreviewGPUFixture fixture;
	return RunTest("ShaderGraphNodePreviewValues", [&] { return TestShaderGraphNodePreviewValues(fixture); }) &&
		RunTest("ShaderGraphGenericNodePreviewValues", [&] { return TestShaderGraphGenericNodePreviewValues(fixture); }) &&
		RunTest("ShaderGraphNodePreviewTextures", [&] { return TestShaderGraphNodePreviewTextures(fixture); }) &&
		RunTest("ShaderGraphNodePreviewDependencies", [&] { return TestShaderGraphNodePreviewDependencies(fixture); }) &&
		RunTest("ShaderGraphNodePreviewCatalog", [&] { return TestShaderGraphNodePreviewCatalog(fixture); });
}
