#include "AnimationClipAssetOverride.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include "AnimationClipManager.h"

// c++
#include <cmath>
#include <unordered_map>

using namespace Engine;

namespace {

	struct ClipOverride {

		std::shared_ptr<const AnimationClipAsset> asset;
		uint64_t revision = 0;
	};
	struct ClipOverrideStorage {

		std::unordered_map<AssetID, ClipOverride> clips;
		uint64_t revision = 0;
	};
}

bool Engine::PublishAnimationClipAssetOverride(ECSWorld& world, AssetID id,
	std::shared_ptr<const AnimationClipAsset> asset) {

	if (!id || !asset || (asset->guid && asset->guid != id) || !std::isfinite(asset->duration) || asset->duration < 0.0f)
		return false;
	auto& storage = world.GetStorage().Get<ClipOverrideStorage>();
	auto& entry = storage.clips[id];
	if (entry.asset == asset) return true;
	entry.asset = std::move(asset);
	entry.revision = ++storage.revision;
	return true;
}

std::shared_ptr<const AnimationClipAsset> Engine::GetAnimationClipAssetOverride(const ECSWorld& world, AssetID id) {

	const auto* storage = world.GetStorage().TryGet<ClipOverrideStorage>();
	if (!storage) return {};
	const auto found = storage->clips.find(id);
	return found == storage->clips.end() ? nullptr : found->second.asset;
}

uint64_t Engine::GetAnimationClipAssetOverrideRevision(const ECSWorld& world, AssetID id) {

	const auto* storage = world.GetStorage().TryGet<ClipOverrideStorage>();
	if (!storage) return 0;
	const auto found = storage->clips.find(id);
	return found == storage->clips.end() ? 0 : found->second.revision;
}

void Engine::ClearAnimationClipAssetOverride(ECSWorld& world, AssetID id) {

	if (auto* storage = world.GetStorage().TryGet<ClipOverrideStorage>()) storage->clips.erase(id);
}

void Engine::ClearAnimationClipAssetOverrides(ECSWorld& world) {

	if (auto* storage = world.GetStorage().TryGet<ClipOverrideStorage>()) storage->clips.clear();
}

const AnimationClipAsset* Engine::ResolveAnimationClipAsset(ECSWorld& world, AnimationClipManager& manager,
	AssetDatabase& database, AssetID id) {

	if (const auto* storage = world.GetStorage().TryGet<ClipOverrideStorage>()) {
		const auto found = storage->clips.find(id);
		if (found != storage->clips.end()) return found->second.asset.get();
	}
	return manager.GetOrLoad(database, id);
}

uint64_t Engine::GetResolvedAnimationClipAssetRevision(const ECSWorld& world, const AnimationClipManager& manager, AssetID id) {

	const auto revision = GetAnimationClipAssetOverrideRevision(world, id);
	return revision ? revision | (uint64_t{1} << 63) : manager.GetRevision(id);
}
