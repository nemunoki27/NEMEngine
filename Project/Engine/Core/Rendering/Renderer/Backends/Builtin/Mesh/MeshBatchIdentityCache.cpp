#include "MeshBatchIdentityCache.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

//============================================================================
//	MeshBatchIdentityCache classMethods
//============================================================================
Engine::MeshBatchIdentityCache::CachedInstance Engine::MeshBatchIdentityCache::MakeInstance(
	const RenderSceneBatch& batch, const RenderItem& item) {

	// 対象と描画設定の更新世代を記録
	CachedInstance result{};
	result.world = item.world;
	result.entity = item.entity;
	result.material = item.material;
	result.batchKey = item.batchKey;
	if (item.world) {
		result.worldLifetime = item.world->GetLifetime();
		result.renderRevision = item.world->GetEntityRenderRevision(item.entity);
		result.resetRevision = item.world->GetRenderResetRevision();
	}
	if (const auto* payload = batch.GetPayload<MeshRenderPayload>(item)) {
		result.subMeshIndex = payload->subMeshIndex;
		result.subMeshGroupIndex = payload->subMeshGroupIndex;
	}
	result.surfaceMode = item.surfaceMode;
	result.phase = item.renderPhase;
	result.blend = item.blendMode;
	result.receiveShadows = item.receiveShadows;
	return result;
}

bool Engine::MeshBatchIdentityCache::Matches(const RenderSceneBatch& batch,
	std::span<const RenderItem* const> items, const MeshGPUResource& gpuMesh) const {

	// 構成とWorldの寿命を照合
	if (items.size() != cachedInstances_.size() || cachedMesh_ != gpuMesh.assetID ||
		cachedMeshGeneration_ != gpuMesh.reloadGeneration) {
		return false;
	}
	for (size_t i = 0; i < items.size(); ++i) {
		auto expected = cachedInstances_[i];
		// 同じアドレスの新Worldへ古いcacheを接続しない
		if (expected.world && !expected.worldLifetime->IsAlive()) {
			return false;
		}
		expected.colorRevision = 0;
		if (expected != MakeInstance(batch, *items[i])) {
			return false;
		}
	}
	return true;
}

void Engine::MeshBatchIdentityCache::Capture(const RenderSceneBatch& batch,
	std::span<const RenderItem* const> items, const MeshGPUResource& gpuMesh) {

	// 新しい構成の識別情報へ置換
	cachedMesh_ = gpuMesh.assetID;
	cachedMeshGeneration_ = gpuMesh.reloadGeneration;
	cachedInstances_.clear();
	for (const auto* item : items) {
		auto entry = MakeInstance(batch, *item);
		entry.colorRevision = item->world ? item->world->GetMeshColorRevision(item->entity) : 0;
		cachedInstances_.push_back(entry);
	}
}

void Engine::MeshBatchIdentityCache::Clear() {

	// Worldの保持を解除
	cachedInstances_.clear();
	cachedMesh_ = {};
	cachedMeshGeneration_ = 0;
}
