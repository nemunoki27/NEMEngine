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

	enum class QuaternionMultiplyOrder :
		uint8_t {

		// baseRotation * curveRotation
		BaseThenCurve,
		// curveRotation * baseRotation
		CurveThenBase,
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

	struct AnimationTrackEditorView {

		// CurveEditorの表示範囲。Runtime評価には使わない。
		float timeMin = 0.0f;
		float timeMax = 1.0f;
		float valueMin = -1.0f;
		float valueMax = 1.0f;
	};

	// Loop終端から0秒へ戻る時だけ使う補間設定
	struct AnimationLoopBridgeSettings {

		bool enabled = false;
		float duration = 0.1f;
		CurveInterpolationMode interpolation = CurveInterpolationMode::Linear;
	};

	// Componentの1プロパティ分のカーブ。
	// Vector3やColor4はチャンネル配列として持ち、CurveEditorへ渡しやすくしておく。
	struct AnimationCurveTrack {

		AnimationPropertyBinding binding;
		// Override/Add/Multiplyの適用方法
		AnimationApplyMode applyMode = AnimationApplyMode::Override;
		// Quaternion Multiply時だけ参照する積の順番
		QuaternionMultiplyOrder quaternionMultiplyOrder = QuaternionMultiplyOrder::BaseThenCurve;
		bool visible = true;
		// TrackごとにCurveEditorのズーム状態を保持する
		AnimationTrackEditorView editorView{};
		std::vector<CurveChannel> channels;
		// QuaternionをAxis/Angleで編集する時だけ使う。Axisチャンネルのキーと同じ順番で保持する。
		std::vector<CurveQuaternionAxisKey> quaternionAxisKeys;
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
		bool autoDuration = false;
		bool loop = false;
		AnimationLoopBridgeSettings loopBridge{};
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

	std::string ToString(QuaternionMultiplyOrder order);
	bool TryParseQuaternionMultiplyOrder(std::string_view text, QuaternionMultiplyOrder& out);

	std::string ToString(CurveInterpolationMode mode);
	bool TryParseCurveInterpolationMode(std::string_view text, CurveInterpolationMode& out);

	uint32_t GetAnimationValueTypeChannelCount(AnimationValueType type);
	std::vector<CurveChannel> MakeDefaultAnimationChannels(AnimationValueType type);
	// JSON互換用に不足チャンネルや表示色を補正する
	void NormalizeAnimationTrackChannels(AnimationCurveTrack& track);
	// autoDuration有効時に最大キー時刻からdurationを更新する
	void UpdateAnimationClipAutoDuration(AnimationClipAsset& clip);

	bool LoadAnimationClipAsset(const std::filesystem::path& path, AnimationClipAsset& outClip);
	bool SaveAnimationClipAsset(const std::filesystem::path& path, const AnimationClipAsset& clip);

	void to_json(nlohmann::json& out, const AnimationClipAsset& clip);
	void from_json(const nlohmann::json& in, AnimationClipAsset& clip);
	void to_json(nlohmann::json& out, const AnimationCurveTrack& track);
	void from_json(const nlohmann::json& in, AnimationCurveTrack& track);
	void to_json(nlohmann::json& out, const AnimationPropertyBinding& binding);
	void from_json(const nlohmann::json& in, AnimationPropertyBinding& binding);
} // Engine
