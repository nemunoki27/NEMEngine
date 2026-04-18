#include "RenderBufferRegistry.h"

//============================================================================
//	RenderBufferRegistry classMethods
//============================================================================

void Engine::RenderBufferRegistry::Register(const RegisteredRenderBuffer& entry) {

	auto found = aliasTable_.find(entry.alias);
	if (found == aliasTable_.end()) {
		aliasTable_[entry.alias] = entries_.size();
		entries_.push_back(entry);
		return;
	}
	entries_[found->second] = entry;
}

void Engine::RenderBufferRegistry::Clear() {

	entries_.clear();
	aliasTable_.clear();
}

Engine::RegisteredRenderBuffer* Engine::RenderBufferRegistry::Find(const std::string& alias) {

	auto found = aliasTable_.find(alias);
	if (found == aliasTable_.end()) {
		return nullptr;
	}
	return &entries_[found->second];
}

const Engine::RegisteredRenderBuffer* Engine::RenderBufferRegistry::Find(const std::string& alias) const {

	auto found = aliasTable_.find(alias);
	if (found == aliasTable_.end()) {
		return nullptr;
	}
	return &entries_[found->second];
}