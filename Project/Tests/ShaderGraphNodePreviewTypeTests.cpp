#include "ShaderGraphNodePreviewCases.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>

// c++
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>

using namespace Engine;

bool NEMTests::TestShaderGraphNodePreviewTypes() {

	struct NumericInput {

		ShaderGraphValueType type;
		MaterialParameterValue value;
	};
	const std::array inputs{
		NumericInput{ShaderGraphValueType::Float, {.5f}},
		NumericInput{ShaderGraphValueType::Float2, {Vector2(.25f, .75f)}},
		NumericInput{ShaderGraphValueType::Float3, {Vector3(.25f, .5f, .75f)}},
		NumericInput{ShaderGraphValueType::Float4, {Vector4(.25f, .5f, .75f, .9f)}},
		NumericInput{ShaderGraphValueType::Color, {Color4(.25f, .5f, .75f, .9f)}},
	};
	TestDirectory directory("ShaderGraphNodePreviewTypes");
	DxShaderCompiler compiler;
	compiler.Init();
	uint32_t outputCount = 0;
	for (const auto& descriptor : ShaderGraphNodeRegistry::GetDescriptors()) {
		if (!descriptor.preview || !std::any_of(descriptor.inputs.begin(), descriptor.inputs.end(),
			[](const ShaderGraphPortDescriptor& port) { return port.type == ShaderGraphValueType::Invalid; })) continue;
		for (const auto& input : inputs) {
			auto testCase = CreateShaderGraphNodePreviewCase(descriptor.kind);
			for (uint32_t slot = 0; slot < descriptor.inputs.size(); ++slot) {
				if (descriptor.inputs[slot].type == ShaderGraphValueType::Invalid) {
					ConnectShaderGraphPreviewValue(testCase, slot, input.type, input.value);
				}
			}
			const uint32_t count = GetShaderGraphOutputCount(testCase.graph.nodes.front());
			for (uint32_t slot = 0; slot < count; ++slot) {
				const auto output = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID,
					slot, {.rawOutput = true}, testCase.resolver);
				const auto path = directory.GetPath() / ("preview" + std::to_string(outputCount++) + ".PS.hlsl");
				std::ofstream stream(path, std::ios::binary);
				stream << output.pixelHLSL;
				stream.close();
				if (!output.Succeeded() || !compiler.CompileShader(path.wstring(), L"ps_6_0", L"main", ShaderStage::PS).IsValid()) {
					std::cerr << "Node type compilation failed: " << descriptor.name << " output=" << slot <<
						" type=" << static_cast<uint32_t>(input.type) << '\n';
					for (const auto& diagnostic : output.diagnostics) std::cerr << diagnostic.message << '\n';
					return false;
				}
			}
		}
	}
	std::cout << "Node preview numeric type outputs: " << outputCount << '\n';
	return true;
}
