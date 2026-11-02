#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Simple/SimpleAnimationPlayback.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/World/Scene/Utility/SceneEntityReference.h>

namespace Engine {

	// 単一値のアニメーション設定と公開する再生状態
	struct SimpleAnimationComponent {

		bool enabled = true;
		bool playOnStart = true;
		bool playInEditMode = false;
		bool autoApply = false;
		SceneEntityReference target{};
		AnimationPropertyBinding binding{};
		SimpleAnimationSettings settings{};

		SimpleAnimationPlayback runtime{};
		Entity runtimeTarget = Entity::Null();
		bool runtimeTargetOverride = false;
		int32_t runtimeRequest = 0;
		float runtimeSeekTime = 0;
		Vector4 runtimeValue{};
		int32_t runtimeInteger = 0;
		int32_t runtimeType = 0;
		float runtimeTime = 0;
		int32_t runtimeCycles = 0;
		bool runtimeValid = false;
		bool runtimePlaying = false;
		bool runtimePaused = false;
		bool runtimeFinished = false;
		std::string runtimeDiagnostic;
	};

	void ApplySimpleAnimationSettings(const SimpleAnimationComponent& source, SimpleAnimationComponent& destination);
	void PublishSimpleAnimationValue(SimpleAnimationComponent& component);
	void from_json(const nlohmann::json& in, SimpleAnimationComponent& component);
	void to_json(nlohmann::json& out, const SimpleAnimationComponent& component);
}
