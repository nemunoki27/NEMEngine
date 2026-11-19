#include "ShaderGraphResourceInputs.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphSourceUtility.h"

// c++
#include <algorithm>
#include <iterator>
#include <utility>
#include <variant>

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

void Engine::ShaderGraphResourceInputs::Normalize(ShaderGraphAsset& graph) {

	auto identifiers = CollectIdentifiers(graph);
	std::vector<ShaderGraphNode> sources;
	constexpr uint64_t kResourceSeed = 0x4E454D5265736F75ull;
	const auto makeID = [&](Engine::UUID owner, uint32_t channel) {
		return MakeUniqueID(identifiers, kResourceSeed, owner, channel);
	};
	const auto addParameter = [&](const ShaderGraphNode& owner, uint32_t slot, const MaterialParameterValue& value) {
		const auto id = makeID(owner.id, 0x100u + slot);
		ShaderGraphParameter parameter{};
		parameter.id = id;
		parameter.name = "TextureInput";
		parameter.referenceName = "TextureInput";
		parameter.type = ShaderGraphValueType::Texture2D;
		parameter.defaultValue = value;
		parameter.exposed = false;
		graph.parameters.emplace_back(std::move(parameter));
		return id;
	};

	for (auto& node : graph.nodes) {
		if (node.kind == ShaderGraphNodeKind::Constant && node.valueType == ShaderGraphValueType::Texture2D) {
			const auto value = std::holds_alternative<AssetID>(node.value.value) ? node.value : MaterialParameterValue{ AssetID{} };
			node.parameterID = addParameter(node, 0, value);
			node.kind = ShaderGraphNodeKind::Parameter;
			continue;
		}
		for (uint32_t slot = 0; slot < node.inputPorts.size() && slot < GetShaderGraphInputCount(node); ++slot) {
			const auto& port = node.inputPorts[slot];
			if (port.type != ShaderGraphValueType::Texture2D || !port.hasDefault ||
				!std::holds_alternative<AssetID>(port.defaultValue.value)) { continue; }
			const bool connected = std::any_of(graph.links.begin(), graph.links.end(),
				[&](const ShaderGraphLink& link) { return link.inputNode == node.id && link.inputSlot == slot; });
			if (connected) { continue; }
			ShaderGraphNode source{};
			source.id = makeID(node.id, 0x200u + slot);
			source.kind = ShaderGraphNodeKind::Parameter;
			source.parameterID = addParameter(node, slot, port.defaultValue);
			source.valueType = ShaderGraphValueType::Texture2D;
			const auto sourceID = source.id;
			sources.emplace_back(std::move(source));
			graph.links.push_back({ makeID(node.id, 0x300u + slot), sourceID, 0, node.id, slot });
		}
	}
	graph.nodes.insert(graph.nodes.end(), std::make_move_iterator(sources.begin()), std::make_move_iterator(sources.end()));
}
