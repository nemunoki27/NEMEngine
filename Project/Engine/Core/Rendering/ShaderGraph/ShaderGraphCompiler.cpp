#include "ShaderGraphCompiler.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphStageSource.h"
#include "ShaderGraphSubGraphExpander.h"
#include "ShaderGraphParameterDefaults.h"

// c++
#include <algorithm>

namespace {

	using namespace Engine;
	using namespace Engine::ShaderGraphSourceUtility;

	// NodeID順にSamplerの配置を決める
	void BuildSamplerBindings(const ShaderGraphAsset& graph, ShaderGraphCompileOutput& output) {

		std::vector<const ShaderGraphNode*> samplerNodes;
		for (const ShaderGraphNode& node : graph.nodes) {
			if (node.kind == ShaderGraphNodeKind::SamplerState) {
				samplerNodes.emplace_back(&node);
			}
		}
		std::sort(samplerNodes.begin(), samplerNodes.end(), [](const ShaderGraphNode* lhs, const ShaderGraphNode* rhs) {
			return lhs->id.value < rhs->id.value;
		});

		// s0は既定サンプラー、s1はレイトレーシングの環境サンプラー
		uint32_t shaderRegister = 2;
		for (const ShaderGraphNode* node : samplerNodes) {
			output.samplers.emplace_back(ShaderGraphSamplerBinding{
				.node = node->id,
				.shaderName = MakeNodeVariable("gSampler", node->id),
				.shaderRegister = shaderRegister++,
				.settings = node->sampler,
			});
		}
	}
}

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;
using namespace Engine::ShaderGraphStageSource;

Engine::ShaderGraphCompileOutput Engine::ShaderGraphCompiler::Compile(const ShaderGraphAsset& graph,
	std::string_view surfaceIncludeFile, const ShaderGraphAssetResolver& resolver) {

	ShaderGraphCompileOutput output{};
	// 元のGraphを保持してSubGraphを展開する
	ShaderGraphAsset expandedGraph = graph;
	if (!ShaderGraphSubGraphExpander::Expand(expandedGraph, resolver, output.diagnostics)) {
		return output;
	}
	// 対象外の頂点出力を生成前に拒否する
	if (expandedGraph.vertexOutputNode && !SupportsShaderGraphVertexOutput(expandedGraph.target)) {

		output.diagnostics.emplace_back(ShaderGraphDiagnostic{
			.severity = ShaderGraphDiagnosticSeverity::Error,
			.stage = ShaderGraphStage::Vertex,
			.node = expandedGraph.vertexOutputNode,
			.port = UINT32_MAX,
			.message = "Vertex出力はMeshまたはPrimitiveでのみ使用できます",
		});
		return output;
	}
	// Scene Textureを参照できる描画方式を確認する
	if (expandedGraph.domain == ShaderGraphDomain::Surface &&
		expandedGraph.surfaceMode != ShaderGraphSurfaceMode::Transparent) {

		for (const ShaderGraphNode& node : expandedGraph.nodes) {
			if (node.kind < ShaderGraphNodeKind::SceneColor || node.kind > ShaderGraphNodeKind::SceneFlags) {
				continue;
			}
			output.diagnostics.emplace_back(ShaderGraphDiagnostic{
				.severity = ShaderGraphDiagnosticSeverity::Error,
				.stage = ShaderGraphStage::Fragment,
				.node = node.id,
				.port = UINT32_MAX,
				.message = "Scene TextureはTransparent Surfaceでのみ使用できます",
			});
		}
		if (!output.Succeeded()) {
			return output;
		}
	}
	// 共通IRの検証結果を生成処理へ渡す
	output.ir = ShaderGraphIRBuilder::Build(expandedGraph);
	output.diagnostics = output.ir.diagnostics;
	if (!output.ir.Succeeded()) {
		return output;
	}
	// 共通のSampler配置から各描画経路を生成する
	BuildSamplerBindings(expandedGraph, output);
	ShaderGraphExpressionCompiler context(expandedGraph, output);
	if (expandedGraph.domain == ShaderGraphDomain::PostProcess) {
		output.computeHLSL = BuildPostProcessSource(expandedGraph, context);
	} else if (expandedGraph.domain == ShaderGraphDomain::RayTracingEffect) {

		output.rayTracingHLSL = BuildRayTracingEffectSource(expandedGraph, context);
	} else {
		output.surfaceHLSL = BuildSurfaceSource(expandedGraph, context);
		if (!output.Succeeded()) {
			return output;
		}
		// 同じSurfaceから不透明と透明のShaderを生成する
		output.opaquePixelHLSL = BuildPixelSource(expandedGraph, surfaceIncludeFile, context, false);
		output.transparentPixelHLSL = BuildPixelSource(expandedGraph, surfaceIncludeFile, context, true);
		if (expandedGraph.target == ShaderGraphTarget::Sprite ||
			expandedGraph.target == ShaderGraphTarget::Primitive2D) {

			output.outlinePixelHLSL = BuildUnlitOutlinePixelSource(
				expandedGraph.target, surfaceIncludeFile, context);
		}
		if (IsShaderGraph3DTarget(expandedGraph.target)) {
			output.rayTracingHLSL = BuildRayTracingSource(surfaceIncludeFile, context);
			output.giMaterialHLSL = BuildGIMaterialSource(surfaceIncludeFile, context);
		}
		if (expandedGraph.target == ShaderGraphTarget::Mesh) {
			// DepthとPickingも同じSurfaceを参照する
			output.depthPixelHLSL = BuildMeshAuxiliaryPixelSource(surfaceIncludeFile, false);
			output.pickingPixelHLSL = BuildMeshAuxiliaryPixelSource(surfaceIncludeFile, true);
			ShaderGraphExpressionCompiler vertexContext(expandedGraph, output);
			// VSとMSはそれぞれの評価状態で頂点処理を生成する
			output.vertexHLSL = BuildMeshVertexSource(expandedGraph, surfaceIncludeFile, vertexContext);
			if (expandedGraph.vertexOutputNode) {

				ShaderGraphExpressionCompiler giContext(expandedGraph, output);
				output.giVertexHLSL = BuildMeshGIVertexSource(expandedGraph, surfaceIncludeFile, giContext);
			}
			ShaderGraphExpressionCompiler meshContext(expandedGraph, output);
			output.meshHLSL = BuildMeshShaderSource(expandedGraph, surfaceIncludeFile, meshContext);
		} else if (expandedGraph.target == ShaderGraphTarget::Primitive3D && expandedGraph.vertexOutputNode) {

			ShaderGraphExpressionCompiler vertexContext(expandedGraph, output);
			output.vertexHLSL = BuildPrimitiveVertexSource(expandedGraph, surfaceIncludeFile, vertexContext);
			ShaderGraphExpressionCompiler giContext(expandedGraph, output);
			output.giVertexHLSL = BuildPrimitiveGIVertexSource(expandedGraph, surfaceIncludeFile, giContext);
			ShaderGraphExpressionCompiler meshContext(expandedGraph, output);
			output.meshHLSL = BuildPrimitiveMeshShaderSource(expandedGraph, surfaceIncludeFile, meshContext);
		} else if (expandedGraph.target == ShaderGraphTarget::Primitive2D && expandedGraph.vertexOutputNode) {

			ShaderGraphExpressionCompiler vertexContext(expandedGraph, output);
			output.vertexHLSL = BuildPrimitive2DVertexSource(expandedGraph, surfaceIncludeFile, vertexContext);
		}
	}
	if (!output.Succeeded()) {
		return output;
	}
	// Materialに公開するParameter情報を作る
	for (const ShaderGraphParameter& parameter : expandedGraph.parameters) {

		output.parameters.emplace_back(ShaderParameterMetadata{
			.shaderName = MakeIdentifier(parameter.referenceName.empty() ? parameter.name : parameter.referenceName,
				parameter.id),
			.displayName = parameter.name,
			.id = MaterialParameterID::FromUUID(parameter.id),
			.semantic = parameter.semantic,
			.isColor = parameter.type == ShaderGraphValueType::Color,
			.isTexture = parameter.type == ShaderGraphValueType::Texture2D,
		});
	}
	// 実行時切替のKeywordだけをParameterとして公開する
	for (const ShaderGraphKeyword& keyword : expandedGraph.keywords) {
		if (!keyword.runtimeToggle) {
			continue;
		}
		output.parameters.emplace_back(ShaderParameterMetadata{
			.shaderName = MakeIdentifier(keyword.referenceName.empty() ? keyword.name : keyword.referenceName, keyword.id),
			.displayName = keyword.name,
			.id = MaterialParameterID::FromUUID(keyword.id),
			.semantic = MaterialParameterSemantic::None,
			.isColor = false,
			.isTexture = false,
		});
	}
	// 展開後の初期値を生成Materialへ渡す
	output.defaultParameters = ShaderGraphParameterDefaults::Build(expandedGraph);
	return output;
}

bool Engine::ShaderGraphCompileOutput::Succeeded() const {

	return std::none_of(diagnostics.begin(), diagnostics.end(), [](const ShaderGraphDiagnostic& diagnostic) {
		return diagnostic.severity == ShaderGraphDiagnosticSeverity::Error;
	});
}
