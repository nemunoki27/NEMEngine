#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/Core/Animation/Curve/AnimationCurve.h>

// c++
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Engine {

	//============================================================================
	//	AnimationClipAsset enum class
	//============================================================================

	// Trackが扱う値の種類。保存形式にもそのまま出すため文字列変換を用意する。
	enum class AnimationValueType :
		uint8_t {

		Float,
		Vector2,
		Vector3,
		Vector4,
		Color3,
		Color4,
		Quaternion,
	};

	// カーブ値を対象プロパティへ反映するときの計算方法。
	// Add/MultiplyはPreview開始時の値を基準にするため、Evaluator側でbase値を受け取る。
	enum class AnimationApplyMode :
		uint8_t {

		Override,
		Add,
		Multiply,
	};

	//============================================================================
	//	AnimationClipAsset structures
	//============================================================================

	// Component名とプロパティパスだけを保存し、Entity固有のUUIDは保存しない。
	struct AnimationPropertyBinding {

		std::string componentName;
		std::string propertyPath;
		AnimationValueType valueType = AnimationValueType::Float;
	};

	// Componentの1プロパティ分のカーブ。
	// Vector3やColor4はチャンネル配列として持ち、CurveEditorへ渡しやすくしておく。
	struct AnimationCurveTrack {

		AnimationPropertyBinding binding;
		AnimationApplyMode applyMode = AnimationApplyMode::Override;
		bool visible = true;
		std::vector<CurveChannel> channels;
	};

	// 将来のEvent Track用。今はJSON上で空配列を維持するための置き場所だけを持つ。
	struct AnimationEventTrack {
	};

	// 再利用可能なAnimationClipアセット本体。
	// Target Entityはツール側だけで持ち、この構造体には保存しない。
	struct AnimationClipAsset {

		AssetID guid{};
		std::string name;
		float duration = 1.0f;
		bool loop = false;
		std::vector<AnimationCurveTrack> curveTracks;
		std::vector<AnimationEventTrack> eventTracks;
	};

	//============================================================================
	//	AnimationClipAsset functions
	//============================================================================

	std::string ToString(AnimationValueType type);
	bool TryParseAnimationValueType(std::string_view text, AnimationValueType& out);

	std::string ToString(AnimationApplyMode mode);
	bool TryParseAnimationApplyMode(std::string_view text, AnimationApplyMode& out);

	std::string ToString(CurveInterpolationMode mode);
	bool TryParseCurveInterpolationMode(std::string_view text, CurveInterpolationMode& out);

	uint32_t GetAnimationValueTypeChannelCount(AnimationValueType type);
	std::vector<CurveChannel> MakeDefaultAnimationChannels(AnimationValueType type);
	void NormalizeAnimationTrackChannels(AnimationCurveTrack& track);

	bool LoadAnimationClipAsset(const std::filesystem::path& path, AnimationClipAsset& outClip);
	bool SaveAnimationClipAsset(const std::filesystem::path& path, const AnimationClipAsset& clip);

	void to_json(nlohmann::json& out, const AnimationClipAsset& clip);
	void from_json(const nlohmann::json& in, AnimationClipAsset& clip);
	void to_json(nlohmann::json& out, const AnimationCurveTrack& track);
	void from_json(const nlohmann::json& in, AnimationCurveTrack& track);
	void to_json(nlohmann::json& out, const AnimationPropertyBinding& binding);
	void from_json(const nlohmann::json& in, AnimationPropertyBinding& binding);
} // Engine
