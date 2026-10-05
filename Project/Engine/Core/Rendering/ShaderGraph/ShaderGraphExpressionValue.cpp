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
		return ShaderGraphExpression{
			parameter.type,
			"graphParameters." + parameterFields_.at(parameter.id.value),
		};
	}
	case ShaderGraphNodeKind::Constant:
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
