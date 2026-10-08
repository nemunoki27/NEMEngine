#include "PipelineBindingCache.h"

//============================================================================
//	PipelineBindingCache classMethods
//============================================================================
Engine::PipelineBindingCache::SlotID Engine::PipelineBindingCache::AddSlot(std::string_view name, ShaderBindingKind kind) {

	// 名前を所有して同期時の検索条件へ登録
	slots_.push_back({ std::string(name), kind, 0, 0, nullptr });
	return static_cast<SlotID>(slots_.size() - 1);
}

Engine::PipelineBindingCache::SlotID Engine::PipelineBindingCache::AddSlotByRegister(
	ShaderBindingKind kind, UINT bindPoint, UINT space) {

	// 名前を空にしてレジスター検索へ登録
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
			// 名前で検索
			slot.location = pipeline.FindBindingByName(slot.name, slot.kind);
		} else {
			// レジスターで検索
			slot.location = pipeline.FindBinding(slot.kind, slot.bindPoint, slot.space);
		}
	}
}

const Engine::RootBindingLocation* Engine::PipelineBindingCache::Get(SlotID id) const {

	// 未登録のIDは未解決として返す
	if (id >= static_cast<SlotID>(slots_.size())) {
		return nullptr;
	}
	return slots_[id].location;
}

bool Engine::PipelineBindingCache::Has(SlotID id) const {

	// 登録範囲と解決結果を確認
	return id < static_cast<SlotID>(slots_.size()) && slots_[id].location != nullptr;
}
