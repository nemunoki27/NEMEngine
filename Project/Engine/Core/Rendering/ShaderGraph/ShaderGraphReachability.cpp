#include "ShaderGraphReachability.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <unordered_set>

using namespace Engine;

void Engine::ShaderGraphReachability::Retain(ShaderGraphAsset& graph, Engine::UUID root,
	std::span<const uint32_t> inputSlots, bool retainParameters) {

	if (!inputSlots.empty()) {
		std::erase_if(graph.links, [&](const ShaderGraphLink& link) {
			return link.inputNode == root && std::find(inputSlots.begin(), inputSlots.end(), link.inputSlot) == inputSlots.end();
		});
	}
	std::unordered_set<uint64_t> reachable;
	std::vector<Engine::UUID> pending{root};
	while (!pending.empty()) {
		const Engine::UUID current = pending.back();
		pending.pop_back();
		if (!reachable.insert(current.value).second) continue;
		for (const ShaderGraphLink& link : graph.links) {
			if (link.inputNode == current) pending.emplace_back(link.outputNode);
		}
	}
	std::erase_if(graph.nodes, [&](const ShaderGraphNode& node) { return !reachable.contains(node.id.value); });
	std::erase_if(graph.links, [&](const ShaderGraphLink& link) { return !reachable.contains(link.inputNode.value); });
	if (!retainParameters) {
		std::erase_if(graph.parameters, [&](const ShaderGraphParameter& parameter) {
			return std::none_of(graph.nodes.begin(), graph.nodes.end(), [&](const ShaderGraphNode& node) {
				return node.kind == ShaderGraphNodeKind::Parameter && node.parameterID == parameter.id;
			});
		});
	}
	std::erase_if(graph.keywords, [&](const ShaderGraphKeyword& keyword) {
		return std::none_of(graph.nodes.begin(), graph.nodes.end(), [&](const ShaderGraphNode& node) {
			return node.kind == ShaderGraphNodeKind::Keyword && node.keywordID == keyword.id;
		});
	});
}
