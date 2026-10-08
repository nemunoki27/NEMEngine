#include "ShaderGraphCompileFixture.h"
#include "ShaderGraphCompileCases.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderReflectionParser.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>

// c++
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

namespace NEMTests {

	// ディザの全描画経路と実際のGPU転送配置を確認する
	bool TestShaderGraphDither(const ShaderGraphCompileFixture& fixture) {

		const auto& generatedRoot = fixture.GetGeneratedRoot();
		const auto& shaderRoot = fixture.GetShaderRoot();
		// 各成分数と画面座標の接続有無でディザを生成する
		for (const auto type : {Engine::ShaderGraphValueType::Float, Engine::ShaderGraphValueType::Float2,
				 Engine::ShaderGraphValueType::Float3, Engine::ShaderGraphValueType::Float4}) {

			for (const bool explicitPosition : {false, true}) {
				auto ditherGraph = Engine::CreateDefaultSurfaceShaderGraph("Dither");
				ditherGraph.renderState.alphaClipping = true;
				std::erase_if(ditherGraph.links,
					[&](const auto& link) { return link.inputNode == ditherGraph.outputNode && link.inputSlot >= 6; });
				auto addNode = [&](Engine::ShaderGraphNodeKind kind) {
					const auto id = Engine::UUID::New();
					ditherGraph.nodes.emplace_back(Engine::ShaderGraphNode{.id = id, .kind = kind});
					return id;
				};
				auto connect = [&](Engine::UUID from, Engine::UUID to, uint32_t slot) {
					ditherGraph.links.emplace_back(Engine::ShaderGraphLink{
						.id = Engine::UUID::New(), .outputNode = from, .inputNode = to, .inputSlot = slot});
				};
				const auto strength = addNode(Engine::ShaderGraphNodeKind::Parameter);
				const auto parameterID = Engine::UUID::New();
				ditherGraph.parameters.emplace_back(
					Engine::ShaderGraphParameter{.id = parameterID, .name = "Strength", .type = type});
				ditherGraph.nodes.back().parameterID = parameterID;
				const auto saturate = addNode(Engine::ShaderGraphNodeKind::Saturate);
				const auto inverse = addNode(Engine::ShaderGraphNodeKind::OneMinus);
				const auto dither = addNode(Engine::ShaderGraphNodeKind::Dither);
				const auto step = addNode(Engine::ShaderGraphNodeKind::Step);
				const auto zero = addNode(Engine::ShaderGraphNodeKind::Constant);
				connect(strength, saturate, 0);
				connect(saturate, inverse, 0);
				connect(inverse, dither, 0);
				connect(dither, step, 1);
				connect(zero, step, 0);
				connect(step, ditherGraph.outputNode, 6);
				const auto threshold = addNode(Engine::ShaderGraphNodeKind::Constant);
				ditherGraph.nodes.back().value.value = 0.5f;
				connect(threshold, ditherGraph.outputNode, 7);
				// Maskedパスにもディザを含む生成シェーダーを適用する
				const Engine::AssetID ditherID{71, 82};
				auto ditherMaterial = Engine::ShaderGraphArtifactCache::CreateMaterial(ditherGraph, ditherID);
				const auto ditherArtifact = Engine::ShaderGraphArtifactCache::DescribeReferences(ditherGraph, ditherID);
				Engine::ShaderGraphArtifactCache::ApplyToMaterial(ditherArtifact, ditherMaterial);
				const auto* masked = Engine::FindPass(ditherMaterial, Engine::MaterialPassKind::Masked);
				if (ditherMaterial.renderState.surfaceMode != Engine::MaterialSurfaceMode::Masked || !masked ||
					masked->pipeline != ditherArtifact.opaquePipelineID ||
					masked->shaderOverride != ditherArtifact.opaqueShaderID) {
					return false;
				}
				if (explicitPosition) {
					connect(addNode(Engine::ShaderGraphNodeKind::ScreenPosition), dither, 1);
				}
				const auto compiled = Engine::ShaderGraphCompiler::Compile(ditherGraph, "surface.hlsli");
				if (compiled.vertexHLSL.empty() || compiled.meshHLSL.empty() ||
					compiled.opaquePixelHLSL.find("StructuredBuffer<ShaderGraphParameters> gMeshMaterialParameters") ==
						std::string::npos ||
					compiled.vertexHLSL.find("#define NEM_SHADER_GRAPH_MATERIAL") == std::string::npos ||
					compiled.meshHLSL.find("#define NEM_SHADER_GRAPH_MATERIAL") == std::string::npos) {
					return false;
				}
				if (!compiled.Succeeded() ||
					compiled.surfaceHLSL.find("ShaderGraphDitherThreshold((graphInput.screenPosition).xy)") ==
						std::string::npos ||
					compiled.surfaceHLSL.find("cell.x * 4u + cell.y") == std::string::npos ||
					compiled.surfaceHLSL.find(" / 17.0f") == std::string::npos ||
					compiled.opaquePixelHLSL.find("clip(graph.baseColor.a * graph.opacity - graph.alphaClip)") ==
						std::string::npos ||
					compiled.depthPixelHLSL.find("clip(graph.baseColor.a * graph.opacity - graph.alphaClip)") ==
						std::string::npos ||
					compiled.pickingPixelHLSL.find("clip(graph.baseColor.a * graph.opacity - graph.alphaClip)") ==
						std::string::npos) {

					return false;
				}
				Engine::ShaderGraphAsset restored;
				if (!Engine::FromJson(Engine::ToJson(ditherGraph), restored) ||
					!fixture.WriteGeneratedGraph(restored,
						std::string("Dither") + std::string(Engine::EnumAdapter<Engine::ShaderGraphValueType>::ToString(type)) +
							(explicitPosition ? "Explicit" : "Default"))) {

					return false;
				}
				// 実際のリフレクションとGPU転送用レイアウトで公開値を検証する
				if (type == Engine::ShaderGraphValueType::Float && !explicitPosition) {
					// テストではプロジェクト探索に依存せずDXCへ明示パスを渡す
					const auto releaseLibrary = [](void* module) { FreeLibrary(static_cast<HMODULE>(module)); };
					std::unique_ptr<void, decltype(releaseLibrary)> library(LoadLibraryW(L"dxcompiler.dll"), releaseLibrary);
					if (!library) {
						return false;
					}
					const auto createInstance = reinterpret_cast<DxcCreateInstanceProc>(
						GetProcAddress(static_cast<HMODULE>(library.get()), "DxcCreateInstance"));
					ComPtr<IDxcUtils> utils;
					ComPtr<IDxcCompiler3> compiler;
					ComPtr<IDxcIncludeHandler> includes;
					ComPtr<IDxcBlobEncoding> source;
					if (!createInstance || FAILED(createInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) ||
						FAILED(createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))) ||
						FAILED(utils->CreateDefaultIncludeHandler(&includes)) ||
						FAILED(
							utils->LoadFile((generatedRoot / "DitherFloatDefault/opaque.PS.hlsl").c_str(), nullptr, &source))) {
						return false;
					}
					const auto graphInclude = (generatedRoot / "DitherFloatDefault").wstring();
					const auto engineInclude = shaderRoot.wstring();
					std::array arguments{
						L"-T", L"ps_6_6", L"-E", L"main", L"-I", engineInclude.c_str(), L"-I", graphInclude.c_str()};
					const DxcBuffer input{source->GetBufferPointer(), source->GetBufferSize(), DXC_CP_UTF8};
					ComPtr<IDxcResult> result;
					HRESULT status = E_FAIL;
					ComPtr<IDxcBlob> reflectionBlob;
					if (FAILED(compiler->Compile(&input, arguments.data(), static_cast<UINT32>(arguments.size()),
							includes.Get(), IID_PPV_ARGS(&result))) ||
						FAILED(result->GetStatus(&status)) || FAILED(status) ||
						FAILED(result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflectionBlob), nullptr))) {
						return false;
					}
					Engine::CompiledShader shader;
					const DxcBuffer reflectionInput{reflectionBlob->GetBufferPointer(), reflectionBlob->GetBufferSize(), 0};
					if (!Engine::ParseDxShaderReflection(
							utils.Get(), reflectionInput, Engine::ShaderStage::PS, shader.reflection)) {
						return false;
					}
					Engine::ShaderAsset metadata;
					metadata.parameters = compiled.parameters;
					Engine::ApplyShaderParameterMetadata(shader.reflection, metadata);
					Engine::MaterialParameterLayout layout;
					layout.Build(shader.reflection, Engine::MaterialParameterCBuffer::kMesh);
					const auto* variable = layout.Find(compiled.parameters.front().id);
					if (!variable || !variable->used || variable->name != "Strength" || variable->valueType != D3D_SVT_FLOAT) {
						return false;
					}
					Engine::MaterialParameterSet overrides;
					overrides.Set(variable->parameterID, variable->name, variable->semantic,
						Engine::MaterialParameterValue{.value = 0.75f});
					const auto packed = Engine::MaterialParameterBufferBuilder::BuildElement({}, overrides, layout, {});
					float strengthValue = 0.0f;
					if (packed.size() < variable->offset + sizeof(strengthValue)) {
						return false;
					}
					std::memcpy(&strengthValue, packed.data() + variable->offset, sizeof(strengthValue));
					if (strengthValue != 0.75f) {
						return false;
					}
				}
				const auto vertex = addNode(Engine::ShaderGraphNodeKind::VertexOutput);
				ditherGraph.vertexOutputNode = vertex;
				connect(dither, vertex, 0);
				if (Engine::ShaderGraphCompiler::Compile(ditherGraph, "surface.hlsli").Succeeded()) {
					return false;
				}
			}
		}

		// しきい値全域で端点と表示数の単調性を確認する
		int previousVisible = 16;
		for (int strengthStep = 0; strengthStep <= 100; ++strengthStep) {
			int visible = 0;
			for (int cell = 1; cell <= 16; ++cell) {
				visible += 1.0f - strengthStep / 100.0f >= cell / 17.0f ? 1 : 0;
			}
			if (visible > previousVisible || (strengthStep == 0 && visible != 16) || (strengthStep == 100 && visible != 0)) {
				return false;
			}
			previousVisible = visible;
		}

		return true;
	}

} // NEMTests
