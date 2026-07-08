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
	++entry.version;
	entry.asset = asset;
}

void Engine::ParticleEffectEditBridge::Remove(AssetID assetID) {

	entries_.erase(assetID);
}

bool Engine::ParticleEffectEditBridge::TryConsume(AssetID assetID,
	uint64_t& lastAppliedVersion, ParticleEffectAsset& outAsset) const {

	auto it = entries_.find(assetID);
	if (it == entries_.end() || it->second.version == lastAppliedVersion) {
		return false;
	}
	lastAppliedVersion = it->second.version;
	outAsset = it->second.asset;
	return true;
}
