#include "ShaderGraphExpressionCompiler.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphBindingNames.h"

// c++
#include <utility>

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

// 描画結果とRayの参照式を作る
ShaderGraphExpression ShaderGraphExpressionCompiler::EmitSceneNode(const ShaderGraphNode& node, uint32_t outputSlot) {

	switch (node.kind) {
	case ShaderGraphNodeKind::SceneColor:
	case ShaderGraphNodeKind::SceneMaterial:
	case ShaderGraphNodeKind::SceneEmissive: {
		ShaderGraphExpression uv = EmitInput(node, 0, ShaderGraphValueType::Float2, "graphInput.uv");
		// 対象の3種類から参照するTextureを決める
		const char* textureName = ShaderGraphBindingNames::kSceneColor;
		if (node.kind == ShaderGraphNodeKind::SceneMaterial) {
			textureName = ShaderGraphBindingNames::kSceneMaterial;
		} else if (node.kind == ShaderGraphNodeKind::SceneEmissive) {
			textureName = ShaderGraphBindingNames::kSceneEmissive;
		} else if (graph_.domain == ShaderGraphDomain::PostProcess) {
			textureName = "gSourceColor";
		}
		const std::string sample = std::string(textureName) + ".SampleLevel(gSampler, " + uv.code + ", 0.0f)";
		switch (outputSlot) {
		case 0:
			return {ShaderGraphValueType::Float4, sample};
		case 1:
			return {ShaderGraphValueType::Float3, "(" + sample + ").rgb"};
		case 2:
			return {ShaderGraphValueType::Float, "(" + sample + ").r"};
		case 3:
			return {ShaderGraphValueType::Float, "(" + sample + ").g"};
		case 4:
			return {ShaderGraphValueType::Float, "(" + sample + ").b"};
		case 5:
			return {ShaderGraphValueType::Float, "(" + sample + ").a"};
		default:
			AddDiagnostic(node.id, "Scene Textureの出力ピンが不正です");
			return {};
		}
	}
	case ShaderGraphNodeKind::SceneDepth:
	case ShaderGraphNodeKind::SceneFlags: {
		const ShaderGraphExpression uv = EmitInput(node, 0, ShaderGraphValueType::Float2, "graphInput.uv");
		if (outputSlot != 0) {
			AddDiagnostic(node.id, "Scene Textureの出力ピンが不正です");
			return {};
		}
		if (node.kind == ShaderGraphNodeKind::SceneFlags) {
			const std::string pixel = graph_.domain == ShaderGraphDomain::PostProcess
										  ? "uint2(saturate(" + uv.code +
												") * "
												"max(resolution - 1.0f.xx, 0.0f.xx))"
										  : "uint2(graphInput.screenPosition.xy)";
			return {
				ShaderGraphValueType::Float,
				"(float)" + std::string(ShaderGraphBindingNames::kSceneFlags) + ".Load(int3(" + pixel + ", 0))",
			};
		}
		return {
			ShaderGraphValueType::Float,
			std::string(ShaderGraphBindingNames::kSceneDepth) + ".SampleLevel(gSampler, " + uv.code + ", 0.0f)",
		};
	}
	case ShaderGraphNodeKind::SceneNormal:
	case ShaderGraphNodeKind::ScenePosition: {
		const ShaderGraphExpression uv = EmitInput(node, 0, ShaderGraphValueType::Float2, "graphInput.uv");
		if (outputSlot != 0) {
			AddDiagnostic(node.id, "Scene Textureの出力ピンが不正です");
			return {};
		}
		const char* textureName = node.kind == ShaderGraphNodeKind::SceneNormal ? ShaderGraphBindingNames::kSceneNormal
																				: ShaderGraphBindingNames::kScenePosition;
		return {
			ShaderGraphValueType::Float3,
			std::string(textureName) + ".SampleLevel(gSampler, " + uv.code + ", 0.0f).xyz",
		};
	}
	case ShaderGraphNodeKind::RayTrace: {
		if (graph_.domain != ShaderGraphDomain::RayTracingEffect) {
			AddDiagnostic(node.id, "Trace SceneはRayTracingEffectでのみ使用できます");
			return {};
		}
		auto cached = customFunctionOutputs_.find(node.id.value);
		if (cached == customFunctionOutputs_.end()) {
			const ShaderGraphExpression origin = EmitInput(node, 0, ShaderGraphValueType::Float3, "graphInput.worldPosition");
			const ShaderGraphExpression direction =
				EmitInput(node, 1, ShaderGraphValueType::Float3, "-graphInput.viewDirection");
			const ShaderGraphExpression minDistance = EmitInput(node, 2, ShaderGraphValueType::Float, "0.001f");
			const ShaderGraphExpression maxDistance = EmitInput(node, 3, ShaderGraphValueType::Float, "gMaxReflectionDistance");
			const ShaderGraphExpression mask =
				EmitInput(node, 4, ShaderGraphValueType::Integer, "int(kRaytracingMaskReflectionCaster)");
			const std::string variable = MakeNodeVariable("trace", node.id);
			evaluationStatements_ += "\tShaderGraphRayResult " + variable + " = ShaderGraphTraceScene(" + origin.code + ", " +
									 direction.code + ", " + minDistance.code + ", " + maxDistance.code + ", (uint)(" +
									 mask.code + "));\n";
			std::vector<ShaderGraphExpression> outputs{
				{ShaderGraphValueType::Float3, variable + ".color"},
				{ShaderGraphValueType::Float, variable + ".hit"},
				{ShaderGraphValueType::Float, variable + ".distance"},
				{ShaderGraphValueType::Float3, variable + ".position"},
				{ShaderGraphValueType::Float3, variable + ".normal"},
			};
			cached = customFunctionOutputs_.emplace(node.id.value, std::move(outputs)).first;
		}
		if (outputSlot >= cached->second.size()) {
			AddDiagnostic(node.id, "Trace Sceneの出力ピンが範囲外です");
			return {};
		}
		return cached->second[outputSlot];
	}
	default:
		return {};
	}
}
