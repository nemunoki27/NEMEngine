#include "ShaderGraphAssetReferences.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Algorithm/HashUtility.h>
#include "ShaderGraphExpressionCompiler.h"
#include "ShaderGraphNodeRegistry.h"
#include "ShaderGraphStageSource.h"
#include "ShaderGraphSubGraphExpander.h"

// c++
#include <algorithm>
#include <optional>
#include <utility>

using namespace Engine;

namespace {

	// 子Graphとの対応を維持しながらピン情報を置き換える
	void PreservePortIDs(std::vector<ShaderGraphPort>& ports, const std::vector<ShaderGraphPort>& previous) {

		std::vector<bool> used(previous.size());
		for (size_t index = 0; index < ports.size(); ++index) {
			auto& port = ports[index];
			const auto find = [&](const auto& predicate) {
				for (size_t oldIndex = 0; oldIndex < previous.size(); ++oldIndex) {
					if (!used[oldIndex] && predicate(previous[oldIndex])) return oldIndex;
				}
				return previous.size();
			};
			size_t match = find([&](const ShaderGraphPort& old) { return old.sourceID && old.sourceID == port.sourceID; });
			if (match == previous.size()) {
				match = find([&](const ShaderGraphPort& old) { return old.name == port.name; });
			}
			if (match == previous.size() && index < previous.size() && !used[index] && !previous[index].sourceID) {
				match = index;
			}
			if (match < previous.size()) {
				used[match] = true;
				port.id = previous[match].id;
			}
			if (!port.id) port.id = Engine::UUID::New();
		}
	}

	// ピンの表示と既定値と参照元を比較する
	bool SamePorts(const std::vector<ShaderGraphPort>& first, const std::vector<ShaderGraphPort>& second) {

		if (first.size() != second.size()) return false;
		for (size_t index = 0; index < first.size(); ++index) {
			const auto& a = first[index];
			const auto& b = second[index];
			if (a.id != b.id || a.sourceID != b.sourceID || a.name != b.name || a.type != b.type ||
				a.hasDefault != b.hasDefault || SerializeMaterialParameterValue(a.defaultValue) != SerializeMaterialParameterValue(b.defaultValue)) {
				return false;
			}
		}
		return true;
	}

	// 同じピンの新しい位置へ接続番号を移す
	std::optional<uint32_t> RemapPortSlot(const std::vector<ShaderGraphPort>& before,
		const std::vector<ShaderGraphPort>& after, uint32_t slot) {

		if (before.empty()) return slot < after.size() ? std::optional<uint32_t>(slot) : std::nullopt;
		if (before.size() <= slot) return std::nullopt;
		const auto found = std::find_if(after.begin(), after.end(), [&](const ShaderGraphPort& port) {
			return port.id == before[slot].id;
		});
		return found == after.end() ? std::nullopt :
			std::optional<uint32_t>(static_cast<uint32_t>(std::distance(after.begin(), found)));
	}
}

ShaderGraphAsset Engine::ShaderGraphAssetReferences::ResolveFunctions(const ShaderGraphAsset& graph,
	const AssetDatabase* database) {

	ShaderGraphAsset resolved = graph;
	if (!database) return resolved;
	for (ShaderGraphNode& node : resolved.nodes) {
		if (node.customFunctionSource != ShaderGraphCustomFunctionSource::File || !node.functionFileAsset) continue;
		node.functionFile = Algorithm::PathToUTF8(database->ResolveFullPath(node.functionFileAsset).lexically_normal());
		std::replace(node.functionFile.begin(), node.functionFile.end(), '\\', '/');
	}
	return resolved;
}

bool Engine::ShaderGraphAssetReferences::LoadSource(AssetID id, const AssetDatabase* database, ShaderGraphAsset& graph) {

	if (!database || !id) return false;
	const auto path = database->ResolveFullPath(id);
	nlohmann::json document;
	if (path.empty() || !JsonAdapter::TryLoad(path, document)) return false;
	try {
		return FromJson(document, graph);
	} catch (const nlohmann::json::exception&) {
		return false;
	}
}

bool Engine::ShaderGraphAssetReferences::Load(AssetID id, const AssetDatabase* database, ShaderGraphAsset& graph) {

	if (!LoadSource(id, database, graph)) return false;
	graph = ResolveFunctions(graph, database);
	return true;
}

bool Engine::ShaderGraphAssetReferences::RefreshSubGraphPorts(ShaderGraphNode& node, const ShaderGraphAsset& child,
	const ShaderGraphAssetResolver& resolver) {

	if (node.kind != ShaderGraphNodeKind::SubGraph) return false;
	const auto output = std::find_if(child.nodes.begin(), child.nodes.end(),
		[&](const ShaderGraphNode& value) { return value.id == child.outputNode; });
	if (output == child.nodes.end()) return false;

	std::vector<ShaderGraphPort> inputs;
	for (const auto& parameter : child.parameters) {
		if (!parameter.exposed) continue;
		inputs.push_back({.name = parameter.name, .type = parameter.type, .defaultValue = parameter.defaultValue,
			.sourceID = parameter.id});
	}
	ShaderGraphAsset expanded = child;
	ShaderGraphCompileOutput compiled;
	const bool canInfer = ShaderGraphSubGraphExpander::Expand(expanded, resolver, compiled.diagnostics);
	ShaderGraphStageSource::BuildSamplerBindings(expanded, compiled);
	ShaderGraphExpressionCompiler expressions(expanded, compiled);
	const auto* descriptor = ShaderGraphNodeRegistry::Find(output->kind);
	std::vector<ShaderGraphPort> outputs;
	for (uint32_t slot = 0; slot < GetShaderGraphInputCount(*output); ++slot) {
		ShaderGraphPort port;
		if (slot < output->inputPorts.size()) {
			port = output->inputPorts[slot];
			port.sourceID = port.id;
			port.id = {};
		} else {
			port.name = GetShaderGraphInputName(*output, slot);
			port.type = descriptor && slot < descriptor->inputs.size() ? descriptor->inputs[slot].type : ShaderGraphValueType::Float;
			port.defaultValue = GetShaderGraphInputDefault(*output, slot).value;
			port.sourceID = Engine::UUID{Algorithm::MixHash(child.outputNode.value, static_cast<uint64_t>(slot) + 1u)};
		}
		if (canInfer) {
			const auto link = std::find_if(expanded.links.begin(), expanded.links.end(), [&](const ShaderGraphLink& value) {
				return value.inputNode == child.outputNode && value.inputSlot == slot;
			});
			if (link != expanded.links.end()) {
				const auto value = expressions.EmitNode(link->outputNode, link->outputSlot);
				if (value.type != ShaderGraphValueType::Invalid) port.type = value.type;
			}
		}
		outputs.push_back(std::move(port));
	}
	PreservePortIDs(inputs, node.inputPorts);
	PreservePortIDs(outputs, node.outputPorts);
	if (SamePorts(inputs, node.inputPorts) && SamePorts(outputs, node.outputPorts)) return false;
	node.inputPorts = std::move(inputs);
	node.outputPorts = std::move(outputs);
	return true;
}

bool Engine::ShaderGraphAssetReferences::RefreshSubGraphs(ShaderGraphAsset& graph, const AssetDatabase* database) {

	if (!database) return false;
	const ShaderGraphAssetResolver resolver = [database](AssetID id, ShaderGraphAsset& child) {
		return Load(id, database, child);
	};
	bool changed = false;
	for (auto& node : graph.nodes) {
		if (node.kind != ShaderGraphNodeKind::SubGraph || !node.subGraph) continue;
		ShaderGraphAsset child;
		if (!Load(node.subGraph, database, child)) continue;
		const auto oldInputs = node.inputPorts;
		const auto oldOutputs = node.outputPorts;
		if (!RefreshSubGraphPorts(node, child, resolver)) continue;
		changed = true;
		const auto previewSlot = RemapPortSlot(oldOutputs, node.outputPorts, node.previewOutputSlot);
		node.previewOutputSlot = previewSlot.value_or(0u);
		std::erase_if(graph.links, [&](const ShaderGraphLink& link) {
			if (link.inputNode == node.id) {
				const auto slot = RemapPortSlot(oldInputs, node.inputPorts, link.inputSlot);
				if (!slot) return true;
			}
			if (link.outputNode == node.id) {
				const auto slot = RemapPortSlot(oldOutputs, node.outputPorts, link.outputSlot);
				if (!slot) return true;
			}
			return false;
		});
		for (auto& link : graph.links) {
			if (link.inputNode == node.id) link.inputSlot = *RemapPortSlot(oldInputs, node.inputPorts, link.inputSlot);
			if (link.outputNode == node.id) link.outputSlot = *RemapPortSlot(oldOutputs, node.outputPorts, link.outputSlot);
		}
	}
	return changed;
}
