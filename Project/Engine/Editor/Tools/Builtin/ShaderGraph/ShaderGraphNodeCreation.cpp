#include "ShaderGraphNodeCreation.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditOperations.h"

// c++
#include <algorithm>
#include <utility>

using Engine::ShaderGraphEditOperations::DefaultValueForGraphType;

Engine::ShaderGraphNodeCreation::ShaderGraphNodeCreation(ShaderGraphEditSession& session) : session_(session) {
}

Engine::UUID Engine::ShaderGraphNodeCreation::AddNode(ShaderGraphNodeKind kind, Vector2 position) {

	// 頂点出力Nodeの重複を避ける
	if (kind == ShaderGraphNodeKind::VertexOutput && session_.GetDraft().vertexOutputNode) {

		return {};
	}

	// Nodeの種類に応じた初期値を設定する
	ShaderGraphNode node{
		.id = UUID::New(),
		.kind = kind,
		.position = position,
	};
	if (kind == ShaderGraphNodeKind::Constant) {
		node.value = DefaultValueForGraphType(node.valueType);
	} else if (kind == ShaderGraphNodeKind::TextureSample) {

		node.value.value = Color4::White();
	} else if (kind == ShaderGraphNodeKind::SamplerState) {

		node.sampler.addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		node.sampler.addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		node.sampler.addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		node.previewExpanded = false;
	} else if (kind == ShaderGraphNodeKind::CustomFunction) {
		// 入出力を持つ既定関数を作成する
		const std::string functionName = "CustomFunction_" + ToString(node.id);
		node.functionName = functionName;
		node.inputPorts.emplace_back(ShaderGraphPort{
			.id = UUID::New(),
			.name = "Input",
			.type = ShaderGraphValueType::Float,
			.defaultValue = DefaultValueForGraphType(ShaderGraphValueType::Float),
		});
		node.outputPorts.emplace_back(ShaderGraphPort{
			.id = UUID::New(),
			.name = "Output",
			.type = ShaderGraphValueType::Float,
			.defaultValue = DefaultValueForGraphType(ShaderGraphValueType::Float),
		});
		node.functionBody = "void " + functionName +
							"(float Input, out float Output) {\n"
							"\tOutput = Input;\n"
							"}";
	}
	const UUID nodeID = Append(std::move(node));
	if (kind == ShaderGraphNodeKind::VertexOutput) {
		session_.GetDraft().vertexOutputNode = nodeID;
	}
	return nodeID;
}

Engine::UUID Engine::ShaderGraphNodeCreation::AddConstantNode(ShaderGraphValueType type, Vector2 position) {

	ShaderGraphNode node{
		.id = UUID::New(),
		.kind = ShaderGraphNodeKind::Constant,
		.valueType = type,
		.value = DefaultValueForGraphType(type),
		.position = position,
		.previewExpanded = false,
	};
	return Append(std::move(node));
}

Engine::UUID Engine::ShaderGraphNodeCreation::AddParameterNode(UUID parameterID, Vector2 position) {

	// 存在するParameterの型を参照する
	const auto parameter = std::find_if(session_.GetDraft().parameters.begin(), session_.GetDraft().parameters.end(),
		[&](const ShaderGraphParameter& value) { return value.id == parameterID; });
	if (parameter == session_.GetDraft().parameters.end()) {
		return {};
	}
	ShaderGraphNode node{
		.id = UUID::New(),
		.kind = ShaderGraphNodeKind::Parameter,
		.parameterID = parameterID,
		.valueType = parameter->type,
		.position = position,
		.previewExpanded = false,
	};
	return Append(std::move(node));
}

Engine::UUID Engine::ShaderGraphNodeCreation::AddKeywordNode(UUID keywordID, Vector2 position) {

	// Keywordの型に合わせて参照Nodeを作成する
	const auto keyword = std::find_if(session_.GetDraft().keywords.begin(), session_.GetDraft().keywords.end(),
		[&](const ShaderGraphKeyword& value) { return value.id == keywordID; });
	if (keyword == session_.GetDraft().keywords.end()) {
		return {};
	}

	ShaderGraphNode node{
		.id = UUID::New(),
		.kind = ShaderGraphNodeKind::Keyword,
		.valueType =
			keyword->type == ShaderGraphKeywordType::Boolean ? ShaderGraphValueType::Boolean : ShaderGraphValueType::Integer,
		.position = position,
		.keywordID = keywordID,
		.previewExpanded = false,
	};
	return Append(std::move(node));
}

Engine::UUID Engine::ShaderGraphNodeCreation::Append(ShaderGraphNode node) {

	// 完成したNodeを編集Graphへ追加する
	const UUID nodeID = node.id;
	session_.GetDraft().nodes.emplace_back(std::move(node));
	session_.MarkDirty();
	return nodeID;
}
