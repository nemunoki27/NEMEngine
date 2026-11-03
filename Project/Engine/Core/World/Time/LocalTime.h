#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Utility/SceneEntityReference.h>

namespace Engine {

	enum class TimeChannel : uint32_t {
		SkinnedAnimation = 1, Particle = 2, SimpleAnimation = 4,
		AnimationClip = 8, Flipbook = 16, MotionAnimation = 32, Timer = 64
	};
	enum class TimeSourceMode : uint8_t { Hierarchy, Explicit, Global, Unscaled };

	struct TimeSourceSettings {
		TimeSourceMode mode = TimeSourceMode::Hierarchy;
		SceneEntityReference source{};
		UUID runtimeSourceID{};
	};
	struct ResolvedLocalTime {
		float deltaTime = 0.0f;
		float localScale = 1.0f;
		bool valid = true;
	};

	Entity ResolveTimeSourceEntity(const ECSWorld& world, Entity owner, const TimeSourceSettings& settings);
	// 保存設定が同じ再適用では実行中の動的な時間源を維持する
	void PreserveTimeSourceRuntime(const TimeSourceSettings& previous, TimeSourceSettings& next);

	// Global時間へ対象チャンネルのローカル倍率を一度だけ適用する
	ResolvedLocalTime ResolveLocalTime(const ECSWorld& world, Entity owner,
		const TimeSourceSettings& settings, TimeChannel channel, float scaledDelta, float unscaledDelta);
	void from_json(const nlohmann::json& in, TimeSourceSettings& settings);
	void to_json(nlohmann::json& out, const TimeSourceSettings& settings);
}
