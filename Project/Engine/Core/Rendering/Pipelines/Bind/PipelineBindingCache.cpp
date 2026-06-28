#include "PipelineBindingCache.h"

//============================================================================
//	PipelineBindingCache classMethods
//============================================================================
Engine::PipelineBindingCache::SlotID Engine::PipelineBindingCache::AddSlot(
	std::string_view name, ShaderBindingKind kind) {

	slots_.push_back({ std::string(name), kind, 0, 0, nullptr });
	return static_cast<SlotID>(slots_.size() - 1);
}

Engine::PipelineBindingCache::SlotID Engine::PipelineBindingCache::AddSlotByRegister(
	ShaderBindingKind kind, UINT bindPoint, UINT space) {

	// 名前を空にしてregister/space検索として登録する
	slots_.push_back({ {}, kind, bindPoint, space, nullptr });
	return static_cast<SlotID>(slots_.size() - 1);
}

void Engine::PipelineBindingCache::Sync(const PipelineState& pipeline) {

	// パイプラインが変わっていなければキャッシュを再利用
	if (pipeline.GetUniqueID() == lastPipelineID_) {
		return;
	}
	lastPipelineID_ = pipeline.GetUniqueID();

	for (SlotEntry& slot : slots_) {

		if (!slot.name.empty()) {
			// 名前ベース検索
			slot.location = pipeline.FindBindingByName(slot.name, slot.kind);
		} else {
			// register/spaceベース検索
			slot.location = pipeline.FindBinding(slot.kind, slot.bindPoint, slot.space);
		}
	}
}

const Engine::RootBindingLocation* Engine::PipelineBindingCache::Get(SlotID id) const {

	if (id >= static_cast<SlotID>(slots_.size())) {
		return nullptr;
	}
	return slots_[id].location;
}

bool Engine::PipelineBindingCache::Has(SlotID id) const {

	return id < static_cast<SlotID>(slots_.size()) && slots_[id].location != nullptr;
}
