#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>
#include <Engine/Core/Animation/Playback/AnimationPlaybackTypes.h>

namespace Engine {

	// 単一クリップの設定と再生状態
	struct AnimationClipPlayerComponent {

		AssetID clip{};
		bool enabled = true;
		bool playOnStart = true;
		bool playInEditMode = false;
		bool unscaledTime = false;
		float speed = 1.0f;
		float startDelay = 0.0f;
		float interval = 0.0f;
		AnimationWrapMode wrapMode = AnimationWrapMode::UseClip;
		int32_t repeatCount = 0;
		int32_t runtimeRequest = 0;
		float runtimeSeekTime = 0.0f;
		float runtimeTime = 0.0f;
		float runtimeDuration = 0.0f;
		bool runtimePlaying = false;
		bool runtimePaused = false;
		bool runtimeFinished = false;
		std::string runtimeDiagnostic;
	};

	void from_json(const nlohmann::json& in, AnimationClipPlayerComponent& component);
	void to_json(nlohmann::json& out, const AnimationClipPlayerComponent& component);
	void ApplyAnimationClipPlayerSettings(const AnimationClipPlayerComponent& source, AnimationClipPlayerComponent& target);
}
