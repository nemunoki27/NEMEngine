#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <memory>
#include <string>
#include <string_view>

namespace Engine {

	class ECSWorld;
	struct SystemContext;
	struct SkinnedMeshAnimationSet;

	struct SkinnedAnimationSample {

		std::string_view clip;
		std::string_view fromClip;
		float time = 0;
		float fromTime = 0;
		float weight = 1;
		bool loop = false;
	};

	// 指定名が無い場合に使用する最初のClipを解決する
	std::string ResolveSkinnedAnimationClip(const SkinnedMeshAnimationSet& animationSet, std::string_view requestedClip);
	// 公開済みの定義とRuntimeの骨格を同じ世代へ揃える
	std::shared_ptr<const SkinnedMeshAnimationSet> PrepareSkinnedAnimation(ECSWorld& world, Entity entity,
		SystemContext& context, bool& definitionChanged);
	// 通常の再生時計を進めず指定時刻の骨格とパレットを生成する
	bool SampleSkinnedAnimation(ECSWorld& world, Entity entity, SystemContext& context,
		const SkinnedAnimationSample& sample, std::string* diagnostic = nullptr);
}
