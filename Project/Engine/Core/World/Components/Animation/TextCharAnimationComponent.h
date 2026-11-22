#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Time/LocalTime.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>
#include <Engine/Core/Foundation/Math/Color.h>

#include <array>
#include <vector>

namespace Engine {

	enum class TextCharAnimationOrder { Simultaneous, LeftToRight, RightToLeft, EndsToCenter, CenterToEnds, Random };
	enum class TextCharAnimationTiming { Interval, TotalDuration };
	enum class TextCharAnimationLines { WholeText, PerLine };
	enum class TextCharAnimationRepeat { Once, LoopText, LoopCharacters };

	struct TextCharAnimationSchedule {
		uint64_t layoutRevision = 0;
		TextCharAnimationOrder order{};
		TextCharAnimationTiming timing{};
		TextCharAnimationLines lines{};
		EasingType easing{};
		float interval = 0;
		float totalDuration = 0;
		float glyphDuration = 0;
		float duration = 0;
		int32_t seed = 0;
		std::vector<float> starts;
	};

	// TextRendererの各文字へ適用するアニメーションと再生状態
	struct TextCharAnimationSettings {
		bool enabled = true;
		bool playOnStart = true;
		bool playInEditMode = false;
		TimeSourceSettings timeSource{};
		SceneEntityReference translationSource{}, rotationSource{}, scaleSource{}, colorSource{};
		std::array<AssetID, 4> sourceAssets{};
		AssetID legacyClip{};
		TextCharAnimationOrder order = TextCharAnimationOrder::LeftToRight;
		TextCharAnimationTiming timing = TextCharAnimationTiming::Interval;
		TextCharAnimationLines lines = TextCharAnimationLines::WholeText;
		TextCharAnimationRepeat repeat = TextCharAnimationRepeat::Once;
		float interval = 0.05f;
		float totalDuration = 1.0f;
		EasingType easing = EasingType::Linear;
		int32_t seed = 0;

		SceneEntityReference& SceneSource(size_t slot);
		const SceneEntityReference& SceneSource(size_t slot) const;
	};

	struct TextCharAnimationComponent : TextCharAnimationSettings {
		UUID runtimeTranslationSourceID{}, runtimeRotationSourceID{}, runtimeScaleSourceID{}, runtimeColorSourceID{};
		int32_t runtimeRequest = 0;
		float runtimeSeekTime = 0;
		float runtimeTime = 0;
		float runtimeDuration = 0;
		bool runtimePlaying = false;
		bool runtimePaused = false;
		bool runtimeFinished = false;
		bool runtimeApplied = false;
		bool runtimeInitialized = false;
		bool runtimeEdit = false;
		bool runtimeEditAuto = false;
		bool runtimeEngaged = false;
		double runtimeClock = 0;
		TextCharAnimationSchedule runtimeSchedule;
		std::string runtimeDiagnostic;
		uint64_t runtimeLayoutRevision = 0;
		std::vector<TextCharAnimationPose> runtimePoses;

		UUID& RuntimeSource(size_t slot);
		const UUID& RuntimeSource(size_t slot) const;
	};

	bool IsTextCharTimingEasing(EasingType easing);
	void from_json(const nlohmann::json& in, TextCharAnimationComponent& component);
	void to_json(nlohmann::json& out, const TextCharAnimationComponent& component);
}
