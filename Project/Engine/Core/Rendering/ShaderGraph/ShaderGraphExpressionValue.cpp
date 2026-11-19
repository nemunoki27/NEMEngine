#include "ShaderGraphExpressionCompiler.h"

//============================================================================
//	include
//============================================================================

// c++
#include <array>

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

// 公開値と組込み入力の式を作る
ShaderGraphExpression ShaderGraphExpressionCompiler::EmitValueNode(const ShaderGraphNode& node, uint32_t outputSlot) {

	switch (node.kind) {
	case ShaderGraphNodeKind::Parameter: {
		const auto found = parameters_.find(node.parameterID.value);
		if (found == parameters_.end()) {
			AddDiagnostic(node.id, "Parameterノードの公開パラメータが見つかりません");
			return {};
		}
		const ShaderGraphParameter& parameter = *found->second;
		if (parameter.type == ShaderGraphValueType::Matrix2 || parameter.type == ShaderGraphValueType::Matrix3 ||
			parameter.type == ShaderGraphValueType::Matrix4 || parameter.type == ShaderGraphValueType::Gradient ||
			parameter.type == ShaderGraphValueType::SamplerState || parameter.type == ShaderGraphValueType::Invalid) {
			AddDiagnostic(node.id, "この型は公開パラメータとして使用できません。専用ノードを使用してください");
			return {};
		}
		return ShaderGraphExpression{
			parameter.type,
			"graphParameters." + parameterFields_.at(parameter.id.value),
		};
	}
	case ShaderGraphNodeKind::Constant:
		if (node.valueType == ShaderGraphValueType::Matrix2 || node.valueType == ShaderGraphValueType::Matrix3 ||
			node.valueType == ShaderGraphValueType::Matrix4 || node.valueType == ShaderGraphValueType::Gradient) {
			auto constant = node;
			constant.kind = node.valueType == ShaderGraphValueType::Matrix2 ? ShaderGraphNodeKind::Matrix2 :
				(node.valueType == ShaderGraphValueType::Matrix3 ? ShaderGraphNodeKind::Matrix3 : ShaderGraphNodeKind::Matrix4);
			if (node.valueType == ShaderGraphValueType::Gradient) {
				constant.kind = ShaderGraphNodeKind::Gradient;
				return EmitGenericInputNode(constant, outputSlot);
			}
			return EmitGenericMatrixNode(constant, outputSlot);
		}
		return ShaderGraphExpression{
			node.valueType,
			MakeLiteral(node.value, node.valueType),
		};
	case ShaderGraphNodeKind::Keyword: {
		const auto found = keywords_.find(node.keywordID.value);
		if (found == keywords_.end()) {
			AddDiagnostic(node.id, "Keywordノードの定義が見つかりません");
			return {};
		}
		const ShaderGraphKeyword& keyword = *found->second;
		if (keyword.runtimeToggle) {
			return ShaderGraphExpression{
				keyword.type == ShaderGraphKeywordType::Boolean ? ShaderGraphValueType::Boolean : ShaderGraphValueType::Integer,
				"graphParameters." + keywordFields_.at(keyword.id.value),
			};
		}
		return ShaderGraphExpression{
			keyword.type == ShaderGraphKeywordType::Boolean ? ShaderGraphValueType::Boolean : ShaderGraphValueType::Integer,
			std::to_string(keyword.defaultIndex) + "u",
		};
	}
	case ShaderGraphNodeKind::UV:
		return ShaderGraphExpression{ShaderGraphValueType::Float2, "graphInput.uv"};
	case ShaderGraphNodeKind::WorldNormal:
		return ShaderGraphExpression{
			ShaderGraphValueType::Float3,
			"graphInput.worldNormal",
		};
	case ShaderGraphNodeKind::WorldPosition:
		return ShaderGraphExpression{
			ShaderGraphValueType::Float3,
			"graphInput.worldPosition",
		};
	case ShaderGraphNodeKind::ObjectPosition:
		return ShaderGraphExpression{
			ShaderGraphValueType::Float3,
			"graphInput.objectPosition",
		};
	case ShaderGraphNodeKind::ObjectNormal:
		return ShaderGraphExpression{
			ShaderGraphValueType::Float3,
			"graphInput.objectNormal",
		};
	case ShaderGraphNodeKind::ObjectTangent:
		return ShaderGraphExpression{
			ShaderGraphValueType::Float3,
			"graphInput.objectTangent",
		};
	case ShaderGraphNodeKind::ViewDirection:
		return ShaderGraphExpression{
			ShaderGraphValueType::Float3,
			"graphInput.viewDirection",
		};
	case ShaderGraphNodeKind::ScreenPosition:
		return ShaderGraphExpression{
			ShaderGraphValueType::Float4,
			"graphInput.screenPosition",
		};
	case ShaderGraphNodeKind::VertexColor:
		return ShaderGraphExpression{
			ShaderGraphValueType::Float4,
			"graphInput.vertexColor",
		};
	case ShaderGraphNodeKind::ParticleData: {
		if (graph_.target != ShaderGraphTarget::Particle && graph_.target != ShaderGraphTarget::Trail) {
			AddDiagnostic(node.id, "Particle DataはParticleまたはTrailで使用できます");
			return {};
		}
		static constexpr std::array expressions{
			"graphInput.particleColor", "graphInput.particleVelocity", "graphInput.particleAge",
			"graphInput.particleLifetime", "graphInput.particleNormalizedAge", "graphInput.particleRandom",
			"graphInput.particleCustomData1", "graphInput.particleCustomData2",
		};
		static constexpr std::array types{ShaderGraphValueType::Float4, ShaderGraphValueType::Float3,
			ShaderGraphValueType::Float, ShaderGraphValueType::Float, ShaderGraphValueType::Float,
			ShaderGraphValueType::Float, ShaderGraphValueType::Float4, ShaderGraphValueType::Float4};
		if (outputSlot >= expressions.size()) return {};
		return {types[outputSlot], expressions[outputSlot]};
	}
	case ShaderGraphNodeKind::Time: {
		static constexpr std::array timeValues{
			"shaderGraphTime",
			"sin(shaderGraphTime)",
			"cos(shaderGraphTime)",
			"shaderGraphDeltaTime",
			"shaderGraphSmoothDeltaTime",
		};
		if (timeValues.size() <= outputSlot) {
			AddDiagnostic(node.id, "Timeの出力ピンが不正です");
			return {};
		}
		return ShaderGraphExpression{
			ShaderGraphValueType::Float,
			timeValues[outputSlot],
		};
	}
	default:
		return {};
	}
}
