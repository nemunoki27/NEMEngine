#include "ShaderGraphExpressionCompiler.h"

//============================================================================
//	include
//============================================================================

// c++
#include <utility>

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

// 外部関数と出力Nodeの診断を作る
ShaderGraphExpression ShaderGraphExpressionCompiler::EmitCustomNode(const ShaderGraphNode& node, uint32_t outputSlot) {

	switch (node.kind) {
	case ShaderGraphNodeKind::CustomFunction: {
		if (node.functionName.empty() || node.outputPorts.empty()) {
			AddDiagnostic(node.id, "Custom Functionの関数名または出力が未設定です");
			return {};
		}
		if (outputSlot >= node.outputPorts.size()) {
			AddDiagnostic(node.id, "Custom Functionの出力ピンが範囲外です");
			return {};
		}
		auto cached = customFunctionOutputs_.find(node.id.value);
		if (cached == customFunctionOutputs_.end()) {
			std::string arguments;
			for (uint32_t slot = 0; slot < node.inputPorts.size(); ++slot) {
				if (!arguments.empty()) { arguments += ", "; }
				arguments += EmitInput(node, slot, node.inputPorts[slot].type,
					MakeLiteral(node.inputPorts[slot].defaultValue, node.inputPorts[slot].type))
								 .code;
			}

			std::vector<ShaderGraphExpression> outputs;
			outputs.reserve(node.outputPorts.size());
			for (uint32_t slot = 0; slot < node.outputPorts.size(); ++slot) {
				const ShaderGraphPort& port = node.outputPorts[slot];
				const std::string variable = MakeNodeVariable("custom" + std::to_string(slot), node.id);
				evaluationStatements_ +=
					"\t" + HLSLType(port.type) + " " + variable + " = " + MakeLiteral(port.defaultValue, port.type) + ";\n";
				if (!arguments.empty()) { arguments += ", "; }
				arguments += variable;
				outputs.emplace_back(ShaderGraphExpression{port.type, variable});
			}
			evaluationStatements_ += "\t" + node.functionName + "(" + arguments + ");\n";
			cached = customFunctionOutputs_.emplace(node.id.value, std::move(outputs)).first;
		}
		return cached->second[outputSlot];
	}
	case ShaderGraphNodeKind::SubGraph:
		AddDiagnostic(node.id, "Sub Graphがコンパイル前に展開されていません");
		return {};
	case ShaderGraphNodeKind::SurfaceOutput:
	case ShaderGraphNodeKind::UnlitOutput:
	case ShaderGraphNodeKind::PostProcessOutput:
	case ShaderGraphNodeKind::RayTracingOutput:
	case ShaderGraphNodeKind::VertexOutput:
		AddDiagnostic(node.id, "Outputノードは値として接続できません");
		return {};
	default:
		return {};
	}
}
