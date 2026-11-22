#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Animation/Simple/SimpleAnimationEvaluation.h>
#include <Engine/Core/World/Components/Animation/TextCharAnimationComponent.h>

// c++
#include <array>
#include <optional>

namespace Engine {

	class AnimationClipManager;
	class AssetDatabase;
	struct SystemContext;

	//============================================================================
	//	TextCharAnimationSampler class
	//	文字のローカル時刻から参照アニメーションの変形と色を求める
	//============================================================================
	class TextCharAnimationSampler {
	public:

		bool Prepare(ECSWorld& world, Entity owner, SystemContext& context,
			const TextCharAnimationComponent& component, std::string& diagnostic);
		float Duration() const { return duration_; }
		TimeChannel Channels() const { return static_cast<TimeChannel>(channels_); }
		TextCharAnimationPose Sample(float localTime, bool loopCharacters) const;

		// 各欄に適用できる参照か調べ、受け付けられない場合は理由を返す
		static const char* ValidateSimple(size_t slot, const SimpleAnimationSettings& settings);
		static const char* ValidateAsset(size_t slot, ECSWorld& world, AssetDatabase& database,
			AnimationClipManager& manager, AssetID asset);
		static uint8_t ClipChannelMask(const AnimationClipAsset& clip);

	private:

		enum class Effect { Translation, Rotation, Scale, Color };

		struct ClipTrack {
			const AnimationCurveTrack* track;
			Effect effect;
			uint8_t mask;
		};
		std::array<std::optional<SimpleAnimationSettings>, 4> simple_{};
		std::array<const AnimationClipAsset*, 4> clips_{};
		std::vector<ClipTrack> tracks_;
		float duration_ = 0.0f;
		uint32_t channels_ = 0;

		// 文字に適用できるカーブと有効なチャンネルを抽出する
		static std::optional<ClipTrack> MatchTrack(const AnimationCurveTrack& track);
		static void Apply(TextCharAnimationPose& pose, Effect effect, const SimpleAnimationValue& value, uint8_t mask);
	};
}
