#include "RaytracingBLASCache.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

void Engine::RaytracingBLASCache::Clear() {

	blases_.clear();
	staticInstanceBLASes_.clear();
	dynamicBlases_.clear();
	meshBLASGeneration_.clear();
}

void Engine::RaytracingBLASCache::CollectExpired() {

	const uint64_t currentFrame =
		GraphicsFrameState::GetFrameSerial();
	auto expired = [currentFrame](uint64_t lastUsedFrame) {
		return currentFrame >
			lastUsedFrame + kGraphicsFrameContextCount;
	};

	// 未使用cacheを間引き、GPU資源は描画Fence完了後に回収する
	std::erase_if(dynamicBlases_, [&](const auto& pair) {
		return !pair.first.worldLifetime || !pair.first.worldLifetime->IsAlive() || expired(pair.second.lastUsedFrame);
	});
	std::erase_if(staticInstanceBLASes_, [&](const auto& pair) {
		return !pair.first.worldLifetime || !pair.first.worldLifetime->IsAlive() || expired(pair.second.lastUsedFrame);
	});

}

namespace Engine {

	bool RaytracingBLASCache::BLASKey::operator==(const BLASKey& rhs) const noexcept {

		return meshAssetID == rhs.meshAssetID &&
			reloadGeneration == rhs.reloadGeneration &&
			lodIndex == rhs.lodIndex &&
			geometryLayoutHash == rhs.geometryLayoutHash;
	}

	size_t RaytracingBLASCache::BLASKeyHash::operator()(const BLASKey& key) const noexcept {

		const size_t h0 = std::hash<AssetID>{}(key.meshAssetID);
		const size_t h1 = std::hash<uint32_t>{}(key.reloadGeneration);
		const size_t h2 = std::hash<uint32_t>{}(key.lodIndex);
		const size_t h3 = std::hash<uint64_t>{}(key.geometryLayoutHash);
		size_t h = h0 ^ (h1 + 0x9e3779b9u + (h0 << 6) + (h0 >> 2));
		h ^= h2 + 0x9e3779b9u + (h << 6) + (h >> 2);
		return h ^ (h3 + 0x9e3779b9u + (h << 6) + (h >> 2));
	}

	bool RaytracingBLASCache::StaticInstanceBLASKey::operator==(const StaticInstanceBLASKey& rhs) const noexcept {

		return worldLifetime == rhs.worldLifetime && world == rhs.world && entity == rhs.entity &&
			meshAssetID == rhs.meshAssetID &&
			reloadGeneration == rhs.reloadGeneration && globalIllumination == rhs.globalIllumination;
	}

	size_t RaytracingBLASCache::StaticInstanceBLASKeyHash::operator()(const StaticInstanceBLASKey& key) const noexcept {

		size_t hash = std::hash<const ECSWorldLifetime*>{}(key.worldLifetime.get());
		hash ^= std::hash<uint32_t>{}(key.entity.index) << 1;
		hash ^= std::hash<uint32_t>{}(key.entity.generation) << 2;
		hash ^= std::hash<AssetID>{}(key.meshAssetID) << 3;
		hash ^= std::hash<uint32_t>{}(key.reloadGeneration) << 4;
		hash ^= std::hash<bool>{}(key.globalIllumination) << 5;
		return hash;
	}

	bool RaytracingBLASCache::DynamicBLASKey::operator==(const DynamicBLASKey& rhs) const noexcept {

		return worldLifetime == rhs.worldLifetime && world == rhs.world && entity == rhs.entity &&
			meshAssetID == rhs.meshAssetID && reloadGeneration == rhs.reloadGeneration && deformationOwner == rhs.deformationOwner;
	}

	size_t RaytracingBLASCache::DynamicBLASKeyHash::operator()(const DynamicBLASKey& key) const noexcept {

		size_t h = std::hash<const ECSWorldLifetime*>{}(key.worldLifetime.get());
		h ^= (std::hash<uint32_t>{}(key.entity.index) << 1);
		h ^= (std::hash<uint32_t>{}(key.entity.generation) << 2);
		h ^= (std::hash<AssetID>{}(key.meshAssetID) << 3);
		h ^= (std::hash<uint32_t>{}(key.reloadGeneration) << 4);
		h ^= std::hash<const void*>{}(key.deformationOwner);
		return h;
	}
}
