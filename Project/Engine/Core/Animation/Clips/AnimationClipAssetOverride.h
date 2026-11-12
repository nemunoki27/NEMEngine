#pragma once

//============================================================================
//	include
//============================================================================
#include "AnimationClipAsset.h"

// c++
#include <memory>

namespace Engine {

	class ECSWorld;
	class AssetDatabase;
	class AnimationClipManager;
	// 保存前のアニメーション編集をWorldの寿命内で共有する
	bool PublishAnimationClipAssetOverride(ECSWorld& world, AssetID id, std::shared_ptr<const AnimationClipAsset> asset);
	std::shared_ptr<const AnimationClipAsset> GetAnimationClipAssetOverride(const ECSWorld& world, AssetID id);
	uint64_t GetAnimationClipAssetOverrideRevision(const ECSWorld& world, AssetID id);
	void ClearAnimationClipAssetOverride(ECSWorld& world, AssetID id);
	void ClearAnimationClipAssetOverrides(ECSWorld& world);
	// 編集世代を優先し通常実行では保存済みキャッシュを使用する
	const AnimationClipAsset* ResolveAnimationClipAsset(ECSWorld& world, AnimationClipManager& manager,
		AssetDatabase& database, AssetID id);
	uint64_t GetResolvedAnimationClipAssetRevision(const ECSWorld& world, const AnimationClipManager& manager, AssetID id);
}
