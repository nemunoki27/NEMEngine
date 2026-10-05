#include "ShaderGraphEditOperations.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>

namespace Engine::ShaderGraphEditOperations {

	// Graphの型に応じた既定値を作る
	MaterialParameterValue DefaultValueForGraphType(ShaderGraphValueType type) {

		Engine::MaterialParameterValue value{};
		switch (type) {
		case Engine::ShaderGraphValueType::Float:
			value.value = 0.0f;
			break;
		case Engine::ShaderGraphValueType::Float2:
			value.value = Engine::Vector2{};
			break;
		case Engine::ShaderGraphValueType::Float3:
			value.value = Engine::Vector3{};
			break;
		case Engine::ShaderGraphValueType::Float4:
			value.value = Engine::Vector4{};
			break;
		case Engine::ShaderGraphValueType::Color:
			value.value = Engine::Color4(1.0f, 1.0f, 1.0f, 1.0f);
			break;
		case Engine::ShaderGraphValueType::Texture2D:
			value.value = Engine::AssetID{};
			break;
		case Engine::ShaderGraphValueType::Boolean:
			value.value = false;
			break;
		case Engine::ShaderGraphValueType::Integer:
			value.value = int32_t{};
			break;
		default:
			value.value = 0.0f;
			break;
		}
		return value;
	}

	// Nodeとその接続を削除する
	void RemoveNode(ShaderGraphAsset& graph, UUID nodeID) {

		if (nodeID == graph.vertexOutputNode) {
			graph.vertexOutputNode = UUID{};
		}

		std::erase_if(graph.nodes, [&](const ShaderGraphNode& node) { return node.id == nodeID; });
		std::erase_if(
			graph.links, [&](const ShaderGraphLink& link) { return link.inputNode == nodeID || link.outputNode == nodeID; });
	}

	void RegenerateNodeIDs(ShaderGraphNode& node) {

		// 元のNodeと入出力PortのIDを引き継がない
		node.id = UUID::New();
		for (ShaderGraphPort& port : node.inputPorts) {
			port.id = UUID::New();
		}
		for (ShaderGraphPort& port : node.outputPorts) {
			port.id = UUID::New();
		}
	}
} // Engine::ShaderGraphEditOperations
