#include "ShaderGraphCompileFixture.h"
#include "ShaderGraphCompileCases.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>

// c++
#include <algorithm>

namespace NEMTests {

	// 既定Graphと各実行領域の生成を確認する
	bool TestShaderGraphDefaultDomains(const ShaderGraphCompileFixture& fixture, const Engine::ShaderGraphAsset& graph) {

		const Engine::ShaderGraphCompileOutput output = Engine::ShaderGraphCompiler::Compile(graph, "NEMTest.surface.hlsli");
		if (!output.Succeeded() || output.parameters.size() != graph.parameters.size() || !graph.parameters.empty() ||
			graph.nodes.size() != 8 || output.surfaceHLSL.find("EvaluateShaderGraphSurface") == std::string::npos ||
			output.surfaceHLSL.find("ShaderGraphTimeConstants") == std::string::npos ||
			output.opaquePixelHLSL.find("EncodeGBuffer") == std::string::npos ||
			output.transparentPixelHLSL.find("EvaluateMeshSurfaceLighting") == std::string::npos ||
			output.rayTracingHLSL.find("ReflectionAnyHit") == std::string::npos ||
			output.rayTracingHLSL.find("ReflectionClosestHit") == std::string::npos) {

			return false;
		}

		Engine::ShaderGraphAsset groupedGraph = graph;
		const Engine::UUID groupID = Engine::UUID::New();
		groupedGraph.groups.emplace_back(Engine::ShaderGraphGroup{
			.id = groupID,
			.name = "NoiseA",
			.position = Engine::Vector2(32.0f, 64.0f),
			.size = Engine::Vector2(320.0f, 180.0f),
		});
		groupedGraph.nodes.front().groupID = groupID;
		Engine::ShaderGraphAsset restoredGroup{};
		if (!Engine::FromJson(Engine::ToJson(groupedGraph), restoredGroup) || restoredGroup.groups.size() != 1 ||
			restoredGroup.nodes.front().groupID != groupID) {

			return false;
		}
		if (!fixture.WriteGeneratedGraph(graph, "Mesh")) {
			return false;
		}

		// Runtime KeywordはMaterial値、Static Keywordは保存時の定数へ変換する
		Engine::ShaderGraphAsset keywordGraph = Engine::CreateDefaultSurfaceShaderGraph("NEMKeywordTest");
		std::erase_if(keywordGraph.links, [&](const Engine::ShaderGraphLink& link) {
			return link.inputNode == keywordGraph.outputNode && link.inputSlot == 2;
		});
		const Engine::UUID keywordID = Engine::UUID::New();
		const Engine::UUID keywordNodeID = Engine::UUID::New();
		keywordGraph.keywords.emplace_back(Engine::ShaderGraphKeyword{
			.id = keywordID,
			.name = "Runtime Feature",
			.referenceName = "RUNTIME_FEATURE",
			.defaultIndex = 1,
			.runtimeToggle = true,
		});
		keywordGraph.nodes.emplace_back(Engine::ShaderGraphNode{
			.id = keywordNodeID,
			.kind = Engine::ShaderGraphNodeKind::Keyword,
			.keywordID = keywordID,
		});
		keywordGraph.links.emplace_back(Engine::ShaderGraphLink{
			.id = Engine::UUID::New(),
			.outputNode = keywordNodeID,
			.inputNode = keywordGraph.outputNode,
			.inputSlot = 2,
		});
		const Engine::ShaderGraphCompileOutput runtimeKeywordOutput =
			Engine::ShaderGraphCompiler::Compile(keywordGraph, "NEMKeywordTest.surface.hlsli");
		const std::string runtimeKeywordName =
			runtimeKeywordOutput.parameters.empty() ? std::string{} : runtimeKeywordOutput.parameters.front().shaderName;
		if (!runtimeKeywordOutput.Succeeded() || runtimeKeywordOutput.parameters.size() != 1 ||
			runtimeKeywordOutput.surfaceHLSL.find("uint " + runtimeKeywordName + ";") == std::string::npos ||
			runtimeKeywordOutput.surfaceHLSL.find("graphParameters." + runtimeKeywordName) == std::string::npos) {
			return false;
		}
		if (!fixture.WriteGeneratedGraph(keywordGraph, "RuntimeKeyword")) {
			return false;
		}
		keywordGraph.keywords.front().runtimeToggle = false;
		const Engine::ShaderGraphCompileOutput staticKeywordOutput =
			Engine::ShaderGraphCompiler::Compile(keywordGraph, "NEMKeywordTest.surface.hlsli");
		if (!staticKeywordOutput.Succeeded() || !staticKeywordOutput.parameters.empty() ||
			staticKeywordOutput.surfaceHLSL.find("uint " + runtimeKeywordName + ";") != std::string::npos ||
			staticKeywordOutput.surfaceHLSL.find("1u") == std::string::npos) {
			return false;
		}

		const Engine::ShaderGraphAsset postProcessGraph = Engine::CreateDefaultPostProcessShaderGraph("NEMPostProcess");
		const Engine::ShaderGraphCompileOutput postProcessOutput =
			Engine::ShaderGraphCompiler::Compile(postProcessGraph, "NEMPostProcess.generated.hlsli");
		Engine::ShaderGraphAsset restoredPostProcess{};
		if (!postProcessOutput.Succeeded() ||
			postProcessOutput.computeHLSL.find("[numthreads(8, 8, 1)]") == std::string::npos ||
			postProcessOutput.computeHLSL.find("gSourceColor.SampleLevel") == std::string::npos ||
			!Engine::FromJson(Engine::ToJson(postProcessGraph), restoredPostProcess) ||
			restoredPostProcess.domain != Engine::ShaderGraphDomain::PostProcess) {
			return false;
		}

		const Engine::ShaderGraphAsset rayTracingGraph =
			Engine::CreateDefaultRayTracingEffectShaderGraph("NEMRayTracingFeature");
		const Engine::ShaderGraphCompileOutput rayTracingOutput =
			Engine::ShaderGraphCompiler::Compile(rayTracingGraph, "NEMRayTracingFeature.generated.hlsli");
		Engine::ShaderGraphAsset restoredRayTracing{};
		if (!rayTracingOutput.Succeeded() ||
			rayTracingOutput.rayTracingHLSL.find("RenderFeatureRayGeneration") == std::string::npos ||
			rayTracingOutput.rayTracingHLSL.find("TraceRay(") == std::string::npos ||
			!Engine::FromJson(Engine::ToJson(rayTracingGraph), restoredRayTracing) ||
			restoredRayTracing.domain != Engine::ShaderGraphDomain::RayTracingEffect ||
			!fixture.WriteGeneratedGraph(rayTracingGraph, "RayTracingFeature")) {

			return false;
		}

		return true;
	}

} // NEMTests
