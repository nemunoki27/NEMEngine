#include "ShaderGraphCompiler.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphStageSource.h"
#include "ShaderGraphSubGraphExpander.h"
#include "ShaderGraphParameterDefaults.h"
#include "ShaderGraphResourceInputs.h"

// c++
#include <algorithm>


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
	ShaderGraphResourceInputs::Normalize(expandedGraph);
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
		if (expandedGraph.target == ShaderGraphTarget::Particle || expandedGraph.target == ShaderGraphTarget::Trail) {
			output.particleGPUPixelHLSL = BuildGPUParticlePixelSource(expandedGraph, surfaceIncludeFile);
		}
		if (expandedGraph.target == ShaderGraphTarget::Sprite ||
			expandedGraph.target == ShaderGraphTarget::Primitive2D) {

			output.outlinePixelHLSL = BuildUnlitOutlinePixelSource(
				expandedGraph.target, surfaceIncludeFile, context);
		}
		if (IsShaderGraph3DTarget(expandedGraph.target)) {
			output.rayTracingHLSL = BuildRayTracingSource(surfaceIncludeFile);
		}
		if (expandedGraph.target == ShaderGraphTarget::Mesh) {
			// DepthとPickingも同じSurfaceを参照する
			output.depthPixelHLSL = BuildMeshAuxiliaryPixelSource(surfaceIncludeFile, false);
			output.pickingPixelHLSL = BuildMeshAuxiliaryPixelSource(surfaceIncludeFile, true);
			ShaderGraphExpressionCompiler vertexContext(expandedGraph, output);
			// VSとMSはそれぞれの評価状態で頂点処理を生成する
			output.vertexHLSL = BuildMeshVertexSource(expandedGraph, surfaceIncludeFile, vertexContext);
			ShaderGraphExpressionCompiler meshContext(expandedGraph, output);
			output.meshHLSL = BuildMeshShaderSource(expandedGraph, surfaceIncludeFile, meshContext);
		} else if (expandedGraph.target == ShaderGraphTarget::Primitive3D) {

			// 頂点出力がなくても標準Material定数を混ぜない
			ShaderGraphExpressionCompiler vertexContext(expandedGraph, output);
			output.vertexHLSL = BuildPrimitiveVertexSource(expandedGraph, surfaceIncludeFile, vertexContext);
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
	BuildParameterBindings(expandedGraph, output);
	return output;
}

bool Engine::ShaderGraphCompileOutput::Succeeded() const {

	return std::none_of(diagnostics.begin(), diagnostics.end(), [](const ShaderGraphDiagnostic& diagnostic) {
		return diagnostic.severity == ShaderGraphDiagnosticSeverity::Error;
	});
}
