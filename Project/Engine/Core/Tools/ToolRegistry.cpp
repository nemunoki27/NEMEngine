#include "ToolRegistry.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>

//============================================================================
//	ToolRegistry classMethods
//============================================================================

Engine::ToolRegistry::~ToolRegistry() {

	Clear();
}

bool Engine::ToolRegistry::Register(std::unique_ptr<ITool> tool) {

	if (!tool) {
		return false;
	}

	const ToolDescriptor& desc = tool->GetDescriptor();
	if (desc.id.empty() || idToIndex_.find(desc.id) != idToIndex_.end()) {
		return false;
	}

	tool->OnRegistered();
	tools_.emplace_back(std::move(tool));
	SortTools();
	RebuildIndex();
	return true;
}

bool Engine::ToolRegistry::Unregister(std::string_view id) {

	auto it = idToIndex_.find(std::string(id));
	if (it == idToIndex_.end()) {
		return false;
	}

	const uint32_t index = it->second;
	tools_[index]->OnUnregistered();
	tools_.erase(tools_.begin() + index);
	RebuildIndex();
	return true;
}

void Engine::ToolRegistry::Clear() {

	for (auto& tool : tools_) {
		if (tool) {
			tool->OnUnregistered();
		}
	}
	tools_.clear();
	idToIndex_.clear();
}

void Engine::ToolRegistry::Tick(ToolContext& context) {

	for (auto& tool : tools_) {
		if (!tool || !tool->IsEnabled(context)) {
			continue;
		}
		tool->Tick(context);
	}
}

Engine::ITool* Engine::ToolRegistry::Find(std::string_view id) {

	auto it = idToIndex_.find(std::string(id));
	if (it == idToIndex_.end()) {
		return nullptr;
	}
	return tools_[it->second].get();
}

const Engine::ITool* Engine::ToolRegistry::Find(std::string_view id) const {

	auto it = idToIndex_.find(std::string(id));
	if (it == idToIndex_.end()) {
		return nullptr;
	}
	return tools_[it->second].get();
}

std::vector<Engine::ITool*> Engine::ToolRegistry::GetTools() {

	std::vector<ITool*> result;
	result.reserve(tools_.size());
	for (auto& tool : tools_) {
		result.emplace_back(tool.get());
	}
	return result;
}

std::vector<const Engine::ITool*> Engine::ToolRegistry::GetTools() const {

	std::vector<const ITool*> result;
	result.reserve(tools_.size());
	for (const auto& tool : tools_) {
		result.emplace_back(tool.get());
	}
	return result;
}

Engine::ToolRegistry& Engine::ToolRegistry::GetInstance() {

	static ToolRegistry registry;
	return registry;
}

void Engine::ToolRegistry::RebuildIndex() {

	idToIndex_.clear();
	for (uint32_t i = 0; i < static_cast<uint32_t>(tools_.size()); ++i) {
		idToIndex_[tools_[i]->GetDescriptor().id] = i;
	}
}

void Engine::ToolRegistry::SortTools() {

	std::sort(tools_.begin(), tools_.end(),
		[](const std::unique_ptr<ITool>& lhs, const std::unique_ptr<ITool>& rhs) {

			const ToolDescriptor& lhsDesc = lhs->GetDescriptor();
			const ToolDescriptor& rhsDesc = rhs->GetDescriptor();
			if (lhsDesc.category != rhsDesc.category) {
				return lhsDesc.category < rhsDesc.category;
			}
			if (lhsDesc.order != rhsDesc.order) {
				return lhsDesc.order < rhsDesc.order;
			}
			return lhsDesc.id < rhsDesc.id;
		});
}
