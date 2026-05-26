#include "GraphNodeRegistry.h"

// c++
#include <algorithm>

//============================================================================
//	GraphNodeRegistry classMethods
//============================================================================

void Engine::GraphNodeRegistry::Clear() {

	// Tool切り替えや再登録時に古い定義を残さない
	definitions_.clear();
}

void Engine::GraphNodeRegistry::Register(const GraphNodeDefinition& definition) {

	// typeが空だと検索Keyにできないため登録しない
	if (definition.type.empty()) {
		return;
	}
	definitions_[definition.type] = definition;
}

const Engine::GraphNodeDefinition* Engine::GraphNodeRegistry::Find(const std::string& type) const {

	// 未登録Typeはnullptrで返し、呼び出し側でUnknown Nodeとして扱う
	const auto it = definitions_.find(type);
	return it != definitions_.end() ? &it->second : nullptr;
}

std::vector<const Engine::GraphNodeDefinition*> Engine::GraphNodeRegistry::GetCreatableDefinitions() const {

	std::vector<const GraphNodeDefinition*> result{};
	result.reserve(definitions_.size());
	for (const auto& [type, definition] : definitions_) {
		(void)type;
		if (definition.canCreateFromMenu) {
			result.emplace_back(&definition);
		}
	}

	// 右クリックメニューの表示順が毎回変わらないように並び替える
	std::sort(result.begin(), result.end(),
		[](const GraphNodeDefinition* lhs, const GraphNodeDefinition* rhs) {
			if (lhs->category == rhs->category) {
				return lhs->displayName < rhs->displayName;
			}
			return lhs->category < rhs->category;
		});
	return result;
}

Engine::GraphNode Engine::GraphNodeRegistry::CreateNode(
	GraphDocument& document, const std::string& type, const ImVec2& position) const {

	GraphNode node{};
	const GraphNodeDefinition* definition = Find(type);
	if (!definition) {
		// 未登録Nodeも読み込めるように、最低限のNode情報だけで生成する
		node.id = document.GenerateID();
		node.type = type;
		node.displayName = type.empty() ? "Unknown" : type;
		node.position = position;
		node.properties = nlohmann::json::object();
		return node;
	}

	node.id = document.GenerateID();
	node.type = definition->type;
	node.displayName = definition->displayName;
	node.position = position;
	node.properties = definition->defaultProperties;

	// 定義から実体ピンを生成する
	for (const GraphPinDefinition& pinDefinition : definition->pins) {

		GraphPin pin{};
		pin.id = document.GenerateID();
		pin.nodeID = node.id;
		pin.kind = pinDefinition.kind;
		pin.valueType = pinDefinition.valueType;
		pin.name = pinDefinition.name;
		pin.required = pinDefinition.required;
		pin.allowMultipleLinks = pinDefinition.allowMultipleLinks;

		if (pin.kind == GraphPinKind::Input) {
			node.inputs.emplace_back(std::move(pin));
		} else {
			node.outputs.emplace_back(std::move(pin));
		}
	}
	return node;
}
