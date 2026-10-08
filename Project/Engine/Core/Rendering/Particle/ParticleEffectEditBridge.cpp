#include "ParticleEffectEditBridge.h"

//============================================================================
//	ParticleEffectEditBridge classMethods
//============================================================================
Engine::ParticleEffectEditBridge& Engine::ParticleEffectEditBridge::GetInstance() {

	static ParticleEffectEditBridge instance;
	return instance;
}

void Engine::ParticleEffectEditBridge::Push(AssetID assetID, const ParticleEffectAsset& asset) {

	if (!assetID) {
		return;
	}

	Entry& entry = entries_[assetID];
	// コピーが成功してから新しい更新として公開する
	ParticleEffectAsset replacement = asset;
	entry.asset = std::move(replacement);
	entry.active = true;
	entry.version = nextVersion_++;
}

void Engine::ParticleEffectEditBridge::Remove(AssetID assetID) {

	auto found = entries_.find(assetID);
	if (found == entries_.end() || !found->second.active) return;
	// 解除も更新として残し、既読consumerへ保存内容の復帰を伝える
	found->second.asset = ParticleEffectAsset{};
	found->second.active = false;
	found->second.version = nextVersion_++;
}

bool Engine::ParticleEffectEditBridge::TryConsume(AssetID assetID,
	uint64_t& lastAppliedVersion, ParticleEffectAsset& outAsset) const {

	auto it = entries_.find(assetID);
	if (it == entries_.end() || !it->second.active || it->second.version == lastAppliedVersion) {
		return false;
	}
	outAsset = it->second.asset;
	lastAppliedVersion = it->second.version;
	return true;
}

uint64_t Engine::ParticleEffectEditBridge::GetRevision(AssetID assetID) const {

	auto found = entries_.find(assetID);
	return found == entries_.end() ? 0 : found->second.version;
}
