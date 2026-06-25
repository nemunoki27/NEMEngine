#include "AnimationClipTool.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>

// imgui
#include <imgui.h>
// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <string_view>
#include <unordered_map>

//============================================================================
//	AnimationClipTool classMethods
//============================================================================
namespace {

	bool DrawEasingComboProperty(const char* label, EasingType& easingType, float reserveRightWidth = 0.0f) {

		if (!MyGUI::BeginPropertyRow(label)) {
			return false;
		}

		const EasingType before = easingType;
		const float width = ImGui::GetContentRegionAvail().x - reserveRightWidth;
		Easing::SelectEasingType(easingType, label, width <= 1.0f ? 1.0f : width);
		MyGUI::EndPropertyRow();
		return before != easingType;
	}

	std::string BuildTrackLabel(const AnimationCurveTrack& track, ECSWorld* world, const Entity& entity) {

		// Component名は冗長なので変数パスだけを表示する
		std::string path = track.binding.propertyPath;
		if (track.binding.componentName != "MeshRenderer") {
			return path;
		}
		// allMeshは全メッシュ、subMeshes[N]は実際のサブメッシュ名で表示する
		if (path.rfind("allMesh", 0) == 0) {
			return "全メッシュ" + path.substr(std::string_view("allMesh").size());
		}
		if (path.rfind("subMeshes[", 0) == 0 && world && world->IsAlive(entity)) {

			const size_t rb = path.find(']');
			if (rb != std::string::npos && rb > 10) {

				uint32_t index = 0;
				bool valid = true;
				for (size_t i = 10; i < rb; ++i) {
					const char c = path[i];
					if (c < '0' || c > '9') { valid = false; break; }
					index = index * 10u + static_cast<uint32_t>(c - '0');
				}
				if (valid) {
					if (MeshRendererComponent* renderer = world->TryGetComponent<MeshRendererComponent>(entity)) {
						if (index < renderer->subMeshes.size() && !renderer->subMeshes[index].name.empty()) {
							return renderer->subMeshes[index].name + path.substr(rb + 1);
						}
					}
				}
			}
		}
		return path;
	}

	const char* DetectedDimensionText(AnimationClipDetectedDimension dimension) {

		switch (dimension) {
		case AnimationClipDetectedDimension::Mode2D:
			return "2D";
		case AnimationClipDetectedDimension::Mode3D:
			return "3D";
		case AnimationClipDetectedDimension::Mixed:
			return "Mixed";
		case AnimationClipDetectedDimension::Unknown:
		default:
			return "Unknown";
		}
	}

	bool Is2DTransformProperty(std::string_view propertyPath) {

		return propertyPath == "localPos2D" ||
			propertyPath == "localRotationZ" ||
			propertyPath == "localScale2D";
	}

	bool Is3DTransformProperty(std::string_view propertyPath) {

		return propertyPath == "localPos" ||
			propertyPath == "localRotation" ||
			propertyPath == "localScale";
	}

	void CollectKeyTimes(std::span<const CurveChannel> channels, std::vector<float>& outTimes) {

		// Channelごとに持っているキー時刻をまとめ、Quaternion編集用に同一時刻を一つに潰す
		outTimes.clear();
		for (const CurveChannel& channel : channels) {
			for (const CurveKey& key : channel.keys) {
				outTimes.emplace_back(key.time);
			}
		}

		std::sort(outTimes.begin(), outTimes.end());
		outTimes.erase(std::unique(outTimes.begin(), outTimes.end(), [](float a, float b) {
			return std::abs(a - b) <= 0.0005f;
			}), outTimes.end());
	}

	CurveInterpolationMode FindKeyInterpolationAt(const CurveChannel& channel, float time) {

		// QuaternionのAxis/Angle編集では代表Channelの補間設定を各時刻へ引き継ぐ
		for (const CurveKey& key : channel.keys) {
			if (std::abs(key.time - time) <= 0.0005f) {
				return key.interpolation;
			}
		}
		return CurveInterpolationMode::Linear;
	}

	CurveQuaternionAxisKey MakeAxisKeyFromQuaternion(const Quaternion& rotation, float& outAngleDegrees) {

		// Clip保存形式はXYZWだが、Editor表示では回転軸と角度へ分解する
		const Quaternion normalized = Quaternion::Normalize(rotation);
		const float w = (std::clamp)(normalized.w, -1.0f, 1.0f);
		const float angleRadians = 2.0f * std::acos(w);
		const float sinHalf = std::sqrt((std::max)(0.0f, 1.0f - w * w));

		Vector3 axis(1.0f, 0.0f, 0.0f);
		if (0.0001f < sinHalf) {
			axis = Vector3(normalized.x / sinHalf, normalized.y / sinHalf, normalized.z / sinHalf);
		}

		CurveQuaternionAxisKey axisKey{};
		axisKey.useCustomAxis = true;
		axisKey.customAxis = axis;
		outAngleDegrees = Math::RadToDeg(angleRadians);
		return QuaternionAxisKeyUtility::Sanitize(axisKey);
	}

	CurveQuaternion BuildQuaternionEditorCurve(const AnimationCurveTrack& track) {

		CurveQuaternion curve{};
		curve.channels[0].keys.clear();
		curve.channels[1].keys.clear();
		curve.axisKeys.clear();

		if (track.channels.size() == 2 && track.channels[0].name == "Axis" && track.channels[1].name == "Angle") {
			curve.channels[0] = track.channels[0];
			curve.channels[1] = track.channels[1];
			curve.axisKeys = track.quaternionAxisKeys;
			curve.EnsureAxisKeyCount();
			return curve;
		}

		std::vector<float> times{};
		CollectKeyTimes(track.channels, times);

		// 各キー時刻でTrackを評価し、編集しやすいAxis/AngleのCurveへ組み替える
		for (float time : times) {
			AnimationPropertyValue value{};
			Quaternion rotation = Quaternion::Identity();
			if (AnimationClipEvaluator::EvaluateTrack(track, time, value)) {
				if (const Quaternion* evaluated = std::get_if<Quaternion>(&value)) {
					rotation = *evaluated;
				}
			}

			float angleDegrees = 0.0f;
			CurveQuaternionAxisKey axisKey = MakeAxisKeyFromQuaternion(rotation, angleDegrees);
			const CurveInterpolationMode interpolation = track.channels.empty() ?
				CurveInterpolationMode::Linear : FindKeyInterpolationAt(track.channels.front(), time);

			curve.channels[0].AddKey(time, 0.0f, interpolation);
			curve.channels[1].AddKey(time, angleDegrees, interpolation);
			curve.axisKeys.emplace_back(axisKey);
		}
		curve.EnsureAxisKeyCount();
		return curve;
	}

	void StoreQuaternionEditorCurve(const CurveQuaternion& curve, AnimationCurveTrack& track) {

		// AxisとAngleを別キーとして残すため、編集後の保存形式も2chのまま保持する
		track.channels = {
			curve.channels[0],
			curve.channels[1],
		};
		track.quaternionAxisKeys = curve.axisKeys;
	}

	void FillChannel(CurveChannel& channel, float value) {

		channel.defaultValue = value;
		channel.keys.clear();
	}

	void SetupTrackInitialValue(AnimationCurveTrack& track, const AnimationPropertyValue& value) {

		track.channels = MakeDefaultAnimationChannels(track.binding.valueType);

		// 追加直後はキーを作らない
		// +ボタンで初めてキーを作るため、defaultValueだけ現在値へ合わせておく
		if (const float* valueFloat = std::get_if<float>(&value)) {
			FillChannel(track.channels[0], *valueFloat);
		} else if (const Vector2* valueVec2 = std::get_if<Vector2>(&value)) {
			FillChannel(track.channels[0], valueVec2->x);
			FillChannel(track.channels[1], valueVec2->y);
		} else if (const Vector3* valueVec3 = std::get_if<Vector3>(&value)) {
			FillChannel(track.channels[0], valueVec3->x);
			FillChannel(track.channels[1], valueVec3->y);
			FillChannel(track.channels[2], valueVec3->z);
		} else if (const Vector4* valueVec4 = std::get_if<Vector4>(&value)) {
			FillChannel(track.channels[0], valueVec4->x);
			FillChannel(track.channels[1], valueVec4->y);
			FillChannel(track.channels[2], valueVec4->z);
			FillChannel(track.channels[3], valueVec4->w);
		} else if (const Color3* valueColor3 = std::get_if<Color3>(&value)) {
			FillChannel(track.channels[0], valueColor3->r);
			FillChannel(track.channels[1], valueColor3->g);
			FillChannel(track.channels[2], valueColor3->b);
		} else if (const Color4* valueColor4 = std::get_if<Color4>(&value)) {
			FillChannel(track.channels[0], valueColor4->r);
			FillChannel(track.channels[1], valueColor4->g);
			FillChannel(track.channels[2], valueColor4->b);
			FillChannel(track.channels[3], valueColor4->a);
		} else if (const Quaternion* valueQuat = std::get_if<Quaternion>(&value)) {
			float angleDegrees = 0.0f;
			MakeAxisKeyFromQuaternion(*valueQuat, angleDegrees);
			FillChannel(track.channels[0], 0.0f);
			FillChannel(track.channels[1], angleDegrees);
			track.quaternionAxisKeys.clear();
		}
	}

	bool FindKeyIndexAtTime(const CurveChannel& channel, float time, uint32_t& outIndex) {

		for (uint32_t i = 0; i < channel.keys.size(); ++i) {
			if (std::abs(channel.keys[i].time - time) <= 0.0005f) {
				outIndex = i;
				return true;
			}
		}
		return false;
	}

	bool CanDrawColorRgbKeyEditor(const AnimationCurveTrack& track, uint32_t selectedChannelIndex) {

		if (track.binding.valueType != AnimationValueType::Color3 &&
			track.binding.valueType != AnimationValueType::Color4) {
			return false;
		}
		// Color4のAlphaはRGBと別キーなので、A選択時はfloat編集だけを表示する
		if (track.binding.valueType == AnimationValueType::Color4 && selectedChannelIndex == 3u) {
			return false;
		}

		constexpr uint32_t kRgbChannelCount = 3u;
		return kRgbChannelCount <= track.channels.size();
	}

	bool IsQuaternionAxisAngleTrack(const AnimationCurveTrack& track) {

		return track.binding.valueType == AnimationValueType::Quaternion &&
			track.channels.size() == 2 &&
			track.channels[0].name == "Axis" &&
			track.channels[1].name == "Angle";
	}

	CurveQuaternionAxisKey& GetQuaternionAxisKeyForEdit(AnimationCurveTrack& track, uint32_t keyIndex) {

		while (track.quaternionAxisKeys.size() <= keyIndex) {
			track.quaternionAxisKeys.emplace_back(QuaternionAxisKeyUtility::MakeDefault());
		}
		return track.quaternionAxisKeys[keyIndex];
	}

	float GetPrimaryAxisValue(const CurveQuaternionAxisKey& axisKey) {

		if (axisKey.axes.empty()) {
			return static_cast<float>(EnumAdapter<Axis>::GetIndex(Axis::X));
		}
		return static_cast<float>(EnumAdapter<Axis>::GetIndex(axisKey.axes.front()));
	}

	void SortQuaternionAxisKeys(AnimationCurveTrack& track) {

		if (!IsQuaternionAxisAngleTrack(track)) {
			return;
		}

		struct AxisKeyPair {

			CurveKey key;
			CurveQuaternionAxisKey axis;
		};

		std::vector<AxisKeyPair> pairs{};
		pairs.reserve(track.channels[0].keys.size());
		for (uint32_t i = 0; i < track.channels[0].keys.size(); ++i) {
			CurveQuaternionAxisKey axisKey = i < track.quaternionAxisKeys.size() ?
				track.quaternionAxisKeys[i] : QuaternionAxisKeyUtility::MakeDefault();
			pairs.push_back({ track.channels[0].keys[i], std::move(axisKey) });
		}
		std::sort(pairs.begin(), pairs.end(), [](const AxisKeyPair& lhs, const AxisKeyPair& rhs) {
			return lhs.key.time < rhs.key.time;
			});

		track.quaternionAxisKeys.resize(pairs.size());
		for (uint32_t i = 0; i < pairs.size(); ++i) {
			track.channels[0].keys[i] = pairs[i].key;
			track.quaternionAxisKeys[i] = std::move(pairs[i].axis);
			track.channels[0].keys[i].value = GetPrimaryAxisValue(track.quaternionAxisKeys[i]);
			track.channels[0].keys[i].interpolation = CurveInterpolationMode::Constant;
		}
	}

	bool DrawColorKeyValueEditor(AnimationCurveTrack& track, uint32_t selectedChannelIndex, float time) {

		// Colorは複数チャンネルを1つの色として見せ足りないキーはその時刻の評価値で補う
		if (!CanDrawColorRgbKeyEditor(track, selectedChannelIndex)) {
			return false;
		}

		constexpr uint32_t kRgbChannelCount = 3u;
		float values[3] = {
			track.channels[0].Evaluate(time),
			track.channels[1].Evaluate(time),
			track.channels[2].Evaluate(time),
		};

		Color3 color(values[0], values[1], values[2]);
		ValueEditResult result = MyGUI::ColorEdit("キー色 RGB", color);
		values[0] = color.r;
		values[1] = color.g;
		values[2] = color.b;

		if (!result.valueChanged) {
			return false;
		}

		for (uint32_t i = 0; i < kRgbChannelCount; ++i) {
			uint32_t keyIndex = 0;
			if (FindKeyIndexAtTime(track.channels[i], time, keyIndex)) {
				track.channels[i].keys[keyIndex].value = values[i];
			} else {
				track.channels[i].AddKey(time, values[i], CurveInterpolationMode::Linear);
			}
		}
		return true;
	}
}

Engine::AnimationClipTool::AnimationClipTool() {

	RegisterBuiltinAnimationProperties();
}

void AnimationClipTool::OpenEditorTool() {

	// ウィンドウ起動
	openWindow_ = true;
}

void AnimationClipTool::DrawEditorTool(const EditorToolContext& context) {

	if (!openWindow_) {
		// Window外でPreviewが残った場合も、次フレームで元の値へ戻す
		EndPreviewAndRestore(context);
		return;
	}

	if (!ImGui::Begin("アニメーションクリップ作成ツール", &openWindow_)) {
		ImGui::End();
		// 折りたたみ中は操作できないため、Preview状態だけは必ず解放する
		EndPreviewAndRestore(context);
		return;
	}

	//============================================================================
	//	AnimationClip編集UI
	//============================================================================
	if (ImGui::BeginTable("AnimationClipToolTopLayout", 2,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {

		ImGui::TableSetupColumn("Toolbar", ImGuiTableColumnFlags_WidthFixed, 420.0f);
		ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextColumn();
		// アセット、編集設定UI
		DrawToolbarUI(context);

		ImGui::TableNextColumn();
		// プロパティ設定UI
		DrawPropertyTreeUI(context);

		ImGui::EndTable();
	}

	ImGui::Separator();

	if (ImGui::BeginTable("AnimationClipToolEditLayout", 2,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {

		ImGui::TableSetupColumn("Curve", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("KeyInspector", ImGuiTableColumnFlags_WidthFixed, 340.0f);

		ImGui::TableNextColumn();
		// プロパティカーブ編集UI
		DrawCurveEditorUI(context);

		ImGui::TableNextColumn();
		DrawKeyInspectorUI(context);
		DrawGeneratorUI(context);

		ImGui::EndTable();
	}

	// 再生中、設定されたターゲットエンティティにアニメーションを直で適用する
	UpdatePreviewPlayback(context);

	ImGui::End();

	if (!openWindow_) {
		EndPreviewAndRestore(context);
	}
}

void AnimationClipTool::DrawToolbarUI(const EditorToolContext& context) {

	// 元のフォントサイズを取得
	float beforeFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
	ImGui::SetWindowFontScale(0.8f);

	//============================================================================
	//	アニメアセットの設定
	//============================================================================
	DrawClipAssetUI(context);

	//============================================================================
	//	再生・編集設定
	//============================================================================
	DrawEditAssetUI(context);

	ImGui::SetWindowFontScale(beforeFontScale);
}

void AnimationClipTool::DrawClipAssetUI(const EditorToolContext& context) {

	AssetID before = clipAssetID_;

	//============================================================================
	//	アセット設定・保存
	//============================================================================
	{
		MyGUI::BeginPropertyRow("アニメクリップのセット");

		// アニメーションクリップアセットのセット
		ValueEditResult result = MyGUI::AssetReferenceField("",
			clipAssetID_, context.toolContext.assetDatabase, { AssetType::AnimationClip },
			{ .useAutoPropertyRow = false,.buttonSize = ImVec2(ImGui::GetContentRegionAvail().x / 2.0f, ImGui::GetFrameHeight()) });
		ImGui::SameLine();
		// 保存ボタン
		if (ImGui::Button("保存", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()))) {

			SaveClipToSelectedAsset(context);
		}
		MyGUI::EndPropertyRow();

		// アセットファイルが変更されたとき
		if (result.valueChanged || before != clipAssetID_) {

			// Clipを差し替える前に、前のClipで適用していたPreview値を必ず戻す
			EndPreviewAndRestore(context);
			LoadClipFromSelectedAsset(context);
		}

		if (!clipErrorText_.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
			ImGui::TextWrapped("%s", clipErrorText_.c_str());
			ImGui::PopStyleColor();
		}
	}
	//============================================================================
	//	アニメ対象エンティティのセット
	//============================================================================
	{
		ECSWorld* world = context.GetWorld();
		UUID nextTargetUUID = targetEntityUUID_;

		MyGUI::BeginPropertyRow("対象エンティティのセット");

		const float clearButtonWidth = 96.0f;
		const float entityFieldWidth = (std::max)(
			ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x, 1.0f);
		const ValueEditResult result = MyGUI::EntityReferenceField("", nextTargetUUID, world,
			{ .useAutoPropertyRow = false,.buttonSize = ImVec2(entityFieldWidth, ImGui::GetFrameHeight()) });
		if (result.valueChanged && nextTargetUUID != targetEntityUUID_) {

			// Targetを変える前に、旧Targetへ適用していたPreview値を戻す
			EndPreviewAndRestore(context);
			targetEntityUUID_ = nextTargetUUID;
			previewTime_ = hasClip_ ? (std::clamp)(previewTime_, 0.0f, clip_.duration) : 0.0f;
			curveState_.currentTime = previewTime_;
			// Targetをセットした直後から、Time 0.0fを含む現在時刻の値をSceneViewへ反映する
			ApplyPreviewAtCurrentTime(context, true);
		}
		ImGui::SameLine();
		const bool hasTargetEntity = targetEntityUUID_ != UUID{};
		if (!hasTargetEntity) {
			ImGui::BeginDisabled();
		}
		if (ImGui::Button("解除", ImVec2(clearButtonWidth, ImGui::GetFrameHeight()))) {

			// 編集中にTargetへ適用していた値を戻してから参照を外す
			EndPreviewAndRestore(context);
			targetEntityUUID_ = {};
		}
		if (!hasTargetEntity) {
			ImGui::EndDisabled();
		}
		MyGUI::EndPropertyRow();
	}
	//============================================================================
	//	アニメーションの再生設定
	//============================================================================
	// アセットが設定されていなければ処理しない
	if (!hasClip_) {
		return;
	}
	{
		// アニメーションを再生する長さ
		float duration = clip_.duration;
		auto result = MyGUI::DragFloat("再生時間", duration, { .dragSpeed = 0.01f,.minValue = 0.01f,
			.maxValue = 10000.0f,.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f });
		if (result.valueChanged) {
			// 必ず0.0f以上に制限する
			clip_.duration = (std::max)(duration, 0.01f);
			previewTime_ = (std::clamp)(previewTime_, 0.0f, clip_.duration);
			curveState_.visibleTimeMax = (std::max)(curveState_.visibleTimeMax, clip_.duration);
			clipDirty_ = true;
		}
		if (MyGUI::Checkbox("最後のキーを再生時間に設定", clip_.autoDuration)) {

			UpdateAnimationClipAutoDuration(clip_);
			previewTime_ = (std::clamp)(previewTime_, 0.0f, AnimationClipEvaluator::GetPlaybackDuration(clip_));
			clipDirty_ = true;
		}

		// 再生開始時の向きを正面として位置/回転を相対適用する、アクション制作向け
		if (MyGUI::Checkbox("向き相対(位置/回転)", clip_.relativeTransform)) {
			// 適用方法が変わるので、現在のPreview値を一度戻してから捕捉し直す
			EndPreviewAndRestore(context);
			clipDirty_ = true;
		}

		ImGui::SeparatorText("ループ再生についての設定");

		if (MyGUI::Checkbox("ループ再生", clip_.loop)) {
			clipDirty_ = true;
		}
		// ループ再生する場合のみの設定
		if (clip_.loop) {

			bool bridgeEnabled = clip_.loopBridge.enabled;
			if (MyGUI::Checkbox("ループのつなぎ補間", bridgeEnabled)) {
				clip_.loopBridge.enabled = bridgeEnabled;
				clipDirty_ = true;
			}
			if (clip_.loopBridge.enabled) {

				result = {};
				result = MyGUI::DragFloat("補間時間", clip_.loopBridge.duration, { .dragSpeed = 0.001f,.minValue = 0.001f,
					.maxValue = 10.0f,.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f });
				if (result.valueChanged) {
					clip_.loopBridge.duration = (std::max)(clip_.loopBridge.duration, 0.001f);
					clipDirty_ = true;
				}
				result = {};
				result = MyGUI::EnumCombo<CurveInterpolationMode>("補間方法", clip_.loopBridge.interpolation,
					{ .reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f });
				if (result.valueChanged) {
					clipDirty_ = true;
				}
			}
		}
	}
}

void Engine::AnimationClipTool::DrawEditAssetUI(const EditorToolContext& context) {

	ImGui::SeparatorText("アニメ編集設定");

	MyGUI::EnumCombo("編集次元の設定", editDimension_, { .reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f });

	if (!hasClip_) {
		return;
	}

	auto result = MyGUI::DragFloat("現在の時間", previewTime_, { .dragSpeed = 0.01f,.minValue = 0.0f,
		.maxValue = clip_.duration,.closeOnProperty = false,.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f });
	if (result.valueChanged) {

		// Scrub中もSceneViewへ即反映し、カーブ編集結果を確認できるようにする
		previewTime_ = (std::clamp)(previewTime_, 0.0f, clip_.duration);
		curveState_.currentTime = previewTime_;
		ApplyPreviewAtCurrentTime(context, true);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%.3f / %.3f", previewTime_, clip_.duration);

	MyGUI::EndPropertyRow();

	MyGUI::DragFloat("再生速度", previewSpeed_, { .dragSpeed = 0.01f,.minValue = 0.01f,
	.maxValue = 8.0f,.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f });

	// 再生/ポーズボタン
	if (ImGui::Button(previewPlaying_ ? "ポーズ" : "再生", ImVec2(100.f, ImGui::GetFrameHeight()))) {
		if (previewPlaying_) {
			previewPlaying_ = false;
		} else {
			// 終端でPlayした時は、AnimationClipの先頭から再生し直す
			if (clip_.duration <= previewTime_) {
				previewTime_ = 0.0f;
				curveState_.currentTime = previewTime_;
			}
			BeginPreview(context);
			previewPlaying_ = previewActive_;
			ApplyPreviewAtCurrentTime(context, true);
		}
	}
	ImGui::SameLine();
	// 停止ボタン
	if (ImGui::Button("停止", ImVec2(100.f, ImGui::GetFrameHeight()))) {

		// StopはPreviewを解除し、編集前のEntity値へ戻したうえで時刻を先頭へ戻す
		EndPreviewAndRestore(context);
		previewTime_ = 0.0f;
		curveState_.currentTime = previewTime_;
	}
}

void AnimationClipTool::DrawPropertyTreeUI(const EditorToolContext& context) {

	if (!hasClip_) {
		return;
	}

	// 元のフォントサイズを取得
	float beforeFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
	ImGui::SetWindowFontScale(0.8f);

	ECSWorld* world = context.GetWorld();
	const Entity targetEntity = GetTargetEntity(context);
	const AnimationClipEditDimension effectiveDimension = GetEffectiveEditDimension(context);

	if (!world || !world->IsAlive(targetEntity)) {
		ImGui::BeginDisabled();
	}

	if (ImGui::Button("プロパティ追加", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()))) {
		ImGui::OpenPopup("AnimationClipAddProperty");
	}
	if (!world || !world->IsAlive(targetEntity)) {
		ImGui::EndDisabled();
	}
	// 追加プロパティ選択のポップアップ
	if (ImGui::BeginPopup("AnimationClipAddProperty")) {
		if (!world || !world->IsAlive(targetEntity)) {
			ImGui::TextDisabled("アニメ対象のエンティティが設定されていません");
		} else {
			// reflection駆動で個別マテリアルパラメータも動的に列挙するためコンテキストを渡す
			AnimationPropertyQueryContext queryContext{};
			queryContext.assetDatabase = context.toolContext.assetDatabase;
			queryContext.renderPipeline = context.panelContext ? context.panelContext->renderPipeline : nullptr;
			const std::vector<AnimationPropertyDescriptor> collectedProperties =
				AnimationPropertyRegistry::GetInstance().CollectProperties(*world, targetEntity, queryContext);

			std::unordered_map<std::string, std::vector<const AnimationPropertyDescriptor*>> groups{};
			for (const AnimationPropertyDescriptor& desc : collectedProperties) {
				if (desc.componentName == "Transform") {
					// 2D/3D表示モードに合わないTransform Propertyは追加候補から外す
					if (effectiveDimension == AnimationClipEditDimension::Mode2D && Is3DTransformProperty(desc.propertyPath)) {
						continue;
					}
					if (effectiveDimension == AnimationClipEditDimension::Mode3D && Is2DTransformProperty(desc.propertyPath)) {
						continue;
					}
				}
				groups[desc.componentName].emplace_back(&desc);
			}
			for (auto& [componentName, properties] : groups) {
				if (!ImGui::BeginMenu(componentName.c_str())) {
					continue;
				}
				for (const AnimationPropertyDescriptor* desc : properties) {

					// 同じClip内に同一Property Trackを複数作らないようにする
					const bool exists = std::any_of(clip_.curveTracks.begin(), clip_.curveTracks.end(),
						[desc](const AnimationCurveTrack& track) {
							return track.binding.componentName == desc->componentName &&
								track.binding.propertyPath == desc->propertyPath;
						});
					if (exists) {
						ImGui::BeginDisabled();
					}
					if (ImGui::MenuItem(desc->displayName.c_str())) {
						AddPropertyTrack(*desc, *world, targetEntity);
						ApplyPreviewAtCurrentTime(context, true);
					}
					if (exists) {
						ImGui::EndDisabled();
					}
				}
				ImGui::EndMenu();
			}
		}
		ImGui::EndPopup();
	}

	if (clip_.curveTracks.empty()) {
		ImGui::TextDisabled("アニメプロパティ無し");
		ImGui::SetWindowFontScale(beforeFontScale);
		return;
	}

	NormalizeSelectedTrackIndex();

	for (size_t i = 0; i < clip_.curveTracks.size();) {

		AnimationCurveTrack& track = clip_.curveTracks[i];
		std::optional<AnimationPropertyDescriptor> desc;
		if (world && world->IsAlive(targetEntity)) {
			desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
				*world, targetEntity, track.binding.componentName, track.binding.propertyPath, track.binding.valueType);
		}
		const bool missing = !world || !world->IsAlive(targetEntity) ||
			!desc || !desc->hasComponent || !desc->hasComponent(*world, targetEntity);

		ImGui::PushID(static_cast<int>(i));
		const bool selected = selectedTrackIndex_ == static_cast<int>(i);

		if (missing) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
		}
		const std::string label = BuildTrackLabel(track, world, targetEntity);

		// 適用コンボ + (順番コンボ) + 削除ボタンの幅を先に確保し、ラベルは残りに詰めて確実に収める
		const ImGuiStyle& style = ImGui::GetStyle();
		const bool isQuaternionTrack = track.binding.valueType == AnimationValueType::Quaternion;
		const bool showOrderCombo = isQuaternionTrack && track.applyMode == AnimationApplyMode::Multiply;
		constexpr float kApplyComboWidth = 90.0f;
		constexpr float kOrderComboWidth = 120.0f;
		const float applyLabelWidth = ImGui::CalcTextSize("適用").x + style.ItemInnerSpacing.x;
		const float deleteWidth = ImGui::CalcTextSize("削除").x + style.FramePadding.x * 2.0f;
		float controlsWidth = kApplyComboWidth + applyLabelWidth + style.ItemSpacing.x + deleteWidth;
		if (showOrderCombo) {
			controlsWidth += kOrderComboWidth + style.ItemSpacing.x;
		}
		const float rowWidth = (std::max)(30.0f, ImGui::GetContentRegionAvail().x - controlsWidth - style.ItemSpacing.x);
		if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_None, ImVec2(rowWidth, 0.0f))) {
			StoreSelectedTrackEditorView();
			selectedTrackIndex_ = static_cast<int>(i);
			LoadSelectedTrackEditorView();
			curveState_.ClearSelection();
			curveState_.frameSelectionRequest = true;
		}
		if (missing) {
			ImGui::PopStyleColor();
			if (ImGui::BeginItemTooltip()) {
				ImGui::TextUnformatted("Target Entity does not have this property.");
				ImGui::EndTooltip();
			}
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(kApplyComboWidth);
		if (isQuaternionTrack) {
			constexpr std::array<int, 2> kQuaternionApplyValues{
				static_cast<int>(AnimationApplyMode::Override),
				static_cast<int>(AnimationApplyMode::Multiply),
			};
			if (ImGui::BeginCombo("適用", EnumAdapter<AnimationApplyMode>::ToString(track.applyMode))) {
				for (int value : kQuaternionApplyValues) {
					const AnimationApplyMode applyMode = static_cast<AnimationApplyMode>(value);
					const bool isSelected = track.applyMode == applyMode;
					if (ImGui::Selectable(EnumAdapter<AnimationApplyMode>::ToString(applyMode), isSelected)) {
						track.applyMode = applyMode;
						clipDirty_ = true;
					}
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			// 積の順番はMultiply時だけ意味を持つので、その時だけ式で選ばせる
			if (showOrderCombo) {

				const auto orderFormula = [](QuaternionMultiplyOrder order) {
					return order == QuaternionMultiplyOrder::BaseThenCurve ? "base * curve" : "curve * base";
					};
				ImGui::SameLine();
				ImGui::SetNextItemWidth(kOrderComboWidth);
				if (ImGui::BeginCombo("##Order", orderFormula(track.quaternionMultiplyOrder))) {
					for (QuaternionMultiplyOrder order : { QuaternionMultiplyOrder::BaseThenCurve, QuaternionMultiplyOrder::CurveThenBase }) {
						const bool isSelected = track.quaternionMultiplyOrder == order;
						if (ImGui::Selectable(orderFormula(order), isSelected)) {
							track.quaternionMultiplyOrder = order;
							clipDirty_ = true;
						}
						if (isSelected) {
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
			}
		} else if (EnumAdapter<AnimationApplyMode>::Combo("適用", &track.applyMode)) {
			clipDirty_ = true;
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("削除")) {
			EndPreviewAndRestore(context);
			clip_.curveTracks.erase(clip_.curveTracks.begin() + i);
			curveState_.ClearSelection();
			if (selectedTrackIndex_ == static_cast<int>(i)) {
				selectedTrackIndex_ = clip_.curveTracks.empty() ? -1 :
					(std::min)(static_cast<int>(i), static_cast<int>(clip_.curveTracks.size()) - 1);
			} else if (static_cast<int>(i) < selectedTrackIndex_) {
				--selectedTrackIndex_;
			}
			clipDirty_ = true;
			ImGui::PopID();
			continue;
		}
		ImGui::PopID();
		++i;
	}
	ImGui::SetWindowFontScale(beforeFontScale);
}

void AnimationClipTool::DrawCurveEditorUI(const EditorToolContext& context) {

	if (!hasClip_) {
		return;
	}

	NormalizeSelectedTrackIndex();

	if (selectedTrackIndex_ < 0) {
		ImGui::TextDisabled("Select property track.");
		return;
	}

	AnimationCurveTrack& track = clip_.curveTracks[static_cast<size_t>(selectedTrackIndex_)];
	if (editorViewTrackIndex_ != selectedTrackIndex_) {
		LoadSelectedTrackEditorView();
	}

	if (track.channels.empty()) {
		ImGui::TextDisabled("Visible curve track is empty.");
		return;
	}

	curveState_.visibleTimeMax = (std::max)(curveState_.visibleTimeMax, clip_.duration);
	curveState_.currentTime = previewTime_;

	const float previousTime = curveState_.currentTime;
	CurveEditSetting curveSetting{};
	curveSetting.size = ImVec2(0.0f, 360.0f);
	curveSetting.autoFit = false;
	curveSetting.showSidePanels = false;
	curveSetting.snap = true;
	curveSetting.snapInterval = 0.001f;
	CurveEditResult result{};
	// 値型ごとにMyGUIのCurve型へ詰め替え、選択TrackだけをEditorに渡す
	switch (track.binding.valueType) {
	case AnimationValueType::Float: {
		CurveFloat curve{};
		curve.channel = track.channels[0];
		result = MyGUI::CurveEditor("AnimationClipCurveEditor", curve, curveState_, curveSetting);
		track.channels[0] = curve.channel;
		break;
	}
	case AnimationValueType::Vector3: {
		if (track.channels.size() == 3) {
			CurveVector3 curve{};
			for (size_t i = 0; i < curve.channels.size(); ++i) {
				curve.channels[i] = track.channels[i];
			}
			result = MyGUI::CurveEditor("AnimationClipCurveEditor", curve, curveState_, curveSetting);
			for (size_t i = 0; i < curve.channels.size(); ++i) {
				track.channels[i] = curve.channels[i];
			}
		}
		break;
	}
	case AnimationValueType::Color3: {
		if (track.channels.size() == 3) {
			CurveColor3 curve{};
			for (size_t i = 0; i < curve.channels.size(); ++i) {
				curve.channels[i] = track.channels[i];
			}
			result = MyGUI::CurveEditor("AnimationClipCurveEditor", curve, curveState_, curveSetting);
			for (size_t i = 0; i < curve.channels.size(); ++i) {
				track.channels[i] = curve.channels[i];
			}
		}
		break;
	}
	case AnimationValueType::Color4: {
		if (track.channels.size() == 4) {
			CurveColor4 curve{};
			for (size_t i = 0; i < curve.channels.size(); ++i) {
				curve.channels[i] = track.channels[i];
			}
			result = MyGUI::CurveEditor("AnimationClipCurveEditor", curve, curveState_, curveSetting);
			for (size_t i = 0; i < curve.channels.size(); ++i) {
				track.channels[i] = curve.channels[i];
			}
		}
		break;
	}
	case AnimationValueType::Quaternion: {
		// QuaternionはAxis/Angleとして編集し、AxisとAngleのキーを別々に保持する
		const bool wasAxisAngleTrack = IsQuaternionAxisAngleTrack(track);
		CurveQuaternion curve = BuildQuaternionEditorCurve(track);
		result = MyGUI::CurveEditor("AnimationClipCurveEditor", curve, curveState_, curveSetting);
		if (!wasAxisAngleTrack || result.valueChanged || result.editFinished) {
			StoreQuaternionEditorCurve(curve, track);
			result.valueChanged |= !wasAxisAngleTrack;
		}
		break;
	}
	case AnimationValueType::Vector2:
	default: {
		std::vector<CurveChannelRef> channelRefs{};
		for (CurveChannel& channel : track.channels) {
			CurveChannelRef ref{};
			ref.channel = &channel;
			ref.displayName = channel.name;
			channelRefs.emplace_back(std::move(ref));
		}
		result = MyGUI::CurveEditor("AnimationClipCurveEditor", channelRefs, curveState_, curveSetting);
		break;
	}
	}

	// Color系はカーブだけでは色変化が分かりにくいので、可視時間範囲の色遷移を帯で表示する
	const bool isColorTrack = track.binding.valueType == AnimationValueType::Color3 ||
		track.binding.valueType == AnimationValueType::Color4;
	if (isColorTrack && track.channels.size() >= 3) {

		const float barHeight = 18.0f;
		const float barWidth = ImGui::GetContentRegionAvail().x;
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		const float timeMin = curveState_.visibleTimeMin;
		const float timeMax = (std::max)(curveState_.visibleTimeMax, timeMin + 0.001f);
		const bool hasAlpha = track.binding.valueType == AnimationValueType::Color4;
		constexpr int kSegments = 64;
		const float segWidth = barWidth / static_cast<float>(kSegments);

		// CurveEditorの時間軸に合わせて区間ごとに色を評価しグラデーションでつなぐ
		auto sampleColor = [&](float ratio) {
			const float time = timeMin + (timeMax - timeMin) * ratio;
			const float r = track.channels[0].Evaluate(time);
			const float g = track.channels[1].Evaluate(time);
			const float b = track.channels[2].Evaluate(time);
			const float a = hasAlpha ? track.channels[3].Evaluate(time) : 1.0f;
			return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
			};
		for (int s = 0; s < kSegments; ++s) {

			const ImU32 left = sampleColor(static_cast<float>(s) / kSegments);
			const ImU32 right = sampleColor(static_cast<float>(s + 1) / kSegments);
			const ImVec2 p0(origin.x + segWidth * static_cast<float>(s), origin.y);
			const ImVec2 p1(origin.x + segWidth * static_cast<float>(s + 1), origin.y + barHeight);
			drawList->AddRectFilledMultiColor(p0, p1, left, right, right, left);
		}
		ImGui::Dummy(ImVec2(barWidth, barHeight));
	}

	StoreSelectedTrackEditorView();

	if (result.valueChanged || result.editFinished) {
		// キー編集で終端時刻が変わるため、Auto Durationをここで再計算する
		UpdateAnimationClipAutoDuration(clip_);
		clipDirty_ = true;
	}
	if (previousTime != curveState_.currentTime || result.valueChanged) {
		// CurveEditor上の時刻移動もToolbarのTimeと同じPreview時刻として扱う
		previewTime_ = (std::clamp)(curveState_.currentTime, 0.0f, clip_.duration);
		curveState_.currentTime = previewTime_;
		ApplyPreviewAtCurrentTime(context, true);
	}
}

void AnimationClipTool::DrawKeyInspectorUI(const EditorToolContext& context) {

	if (selectedTrackIndex_ < 0 || static_cast<int>(clip_.curveTracks.size()) <= selectedTrackIndex_) {
		return;
	}
	if (curveState_.selectedKeys.empty()) {
		ImGui::SeparatorText("キーインスペクター");
		ImGui::TextDisabled("キーが選択されていません");
		return;
	}

	AnimationCurveTrack& track = clip_.curveTracks[static_cast<size_t>(selectedTrackIndex_)];
	CurveKeySelection selection = curveState_.selectedKeys.front();
	if (track.channels.size() <= selection.channelIndex ||
		track.channels[selection.channelIndex].keys.size() <= selection.keyIndex) {
		return;
	}

	CurveChannel& channel = track.channels[selection.channelIndex];
	CurveKey& key = channel.keys[selection.keyIndex];

	ImGui::SeparatorText("キーインスペクター");
	ImGui::Text("チャンネル: %s", channel.name.c_str());
	bool changed = false;
	float time = key.time;
	if (MyGUI::DragFloat("キー時間", time, { .dragSpeed = 0.001f,.minValue = 0.0f,
		.maxValue = 10000.0f,.reserveRightWidth = ImGui::GetContentRegionAvail().x / 4.0f }).valueChanged) {
		key.time = (std::max)(0.0f, time);
		changed = true;
	}

	if (IsQuaternionAxisAngleTrack(track) && selection.channelIndex == 0u) {

		CurveQuaternionAxisKey& axisKey = GetQuaternionAxisKeyForEdit(track, selection.keyIndex);
		if (MyGUI::Checkbox("カスタム軸", axisKey.useCustomAxis)) {
			changed = true;
		}
		if (axisKey.useCustomAxis) {
			Vector3 customAxis = axisKey.customAxis;
			if (MyGUI::DragVector3("キー回転軸", customAxis, { .dragSpeed = 0.001f,.minValue = -1.0f,.maxValue = 1.0f }).valueChanged) {
				axisKey.customAxis = customAxis;
				changed = true;
			}
		} else {
			if (axisKey.axes.empty()) {
				axisKey.axes.emplace_back(Axis::X);
			}
			Axis axis = axisKey.axes.front();
			if (MyGUI::EnumCombo("キー回転軸", axis, {
				.reserveRightWidth = ImGui::GetContentRegionAvail().x / 6.0f }).valueChanged) {
				axisKey.axes = { axis };
				changed = true;
			}
		}
		key.value = GetPrimaryAxisValue(axisKey);
		key.interpolation = CurveInterpolationMode::Constant;
	} else if (IsQuaternionAxisAngleTrack(track) && selection.channelIndex == 1u) {

		if (MyGUI::DragFloat("キー角度", key.value, { .dragSpeed = 0.1f,.minValue = -36000.0f,
			.maxValue = 36000.0f,.reserveRightWidth = ImGui::GetContentRegionAvail().x / 4.0f }).valueChanged) {
			changed = true;
		}
	} else if (CanDrawColorRgbKeyEditor(track, selection.channelIndex)) {
		if (DrawColorKeyValueEditor(track, selection.channelIndex, key.time)) {
			changed = true;
		}
	} else {
		if (MyGUI::DragFloat("キー値", key.value, { .dragSpeed = 0.001f,.minValue = -100000.0f,
			.maxValue = 100000.0f,.reserveRightWidth = ImGui::GetContentRegionAvail().x / 4.0f }).valueChanged) {
			changed = true;
		}
	}

	const bool quaternionTrack = track.binding.valueType == AnimationValueType::Quaternion;
	CurveInterpolationMode interpolation = key.interpolation;
	if (!quaternionTrack && interpolation == CurveInterpolationMode::Squad) {
		// SquadはQuaternion専用なので、通常ChannelではSplineとして表示する
		interpolation = CurveInterpolationMode::Spline;
	}

	if (!(IsQuaternionAxisAngleTrack(track) && selection.channelIndex == 0u) &&
		MyGUI::BeginPropertyRow("補間方法")) {
		const float width = ImGui::GetContentRegionAvail().x - ImGui::GetContentRegionAvail().x / 2.0f;
		ImGui::SetNextItemWidth(width <= 1.0f ? 1.0f : width);
		if (quaternionTrack) {

			if (EnumAdapter<CurveInterpolationMode>::Combo("##Value", &interpolation)) {
				key.interpolation = interpolation;
				changed = true;
			}
		} else {

			constexpr std::array<CurveInterpolationMode, 4> kNonQuaternionInterpolations{
				CurveInterpolationMode::Constant,
				CurveInterpolationMode::Linear,
				CurveInterpolationMode::Bezier,
				CurveInterpolationMode::Spline,
			};

			if (ImGui::BeginCombo("##Value", EnumAdapter<CurveInterpolationMode>::ToString(interpolation))) {
				for (CurveInterpolationMode mode : kNonQuaternionInterpolations) {

					const bool selected = interpolation == mode;
					if (ImGui::Selectable(EnumAdapter<CurveInterpolationMode>::ToString(mode), selected)) {
						key.interpolation = mode;
						changed = true;
					}
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
		}
		MyGUI::EndPropertyRow();
	}

	if (key.interpolation == CurveInterpolationMode::Bezier) {
		Vector2 inTangent = key.inTangent;
		if (MyGUI::DragVector2("入力タンジェント", inTangent, { .dragSpeed = 0.001f,.minValue = -10000.0f,
			.maxValue = 10000.0f,.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f }).valueChanged) {
			key.inTangent = inTangent;
			changed = true;
		}
		Vector2 outTangent = key.outTangent;
		if (MyGUI::DragVector2("出力タンジェント", outTangent, { .dragSpeed = 0.001f,.minValue = -10000.0f,
			.maxValue = 10000.0f,.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f }).valueChanged) {
			key.outTangent = outTangent;
			changed = true;
		}
	}

	if (changed) {
		const float editedTime = key.time;
		if (IsQuaternionAxisAngleTrack(track) && selection.channelIndex == 0u) {
			SortQuaternionAxisKeys(track);
		} else {
			channel.SortKeys();
		}
		// 時刻変更で並びが変わるため、編集していたKeyを再選択する
		for (uint32_t i = 0; i < channel.keys.size(); ++i) {
			if (std::abs(channel.keys[i].time - editedTime) <= 0.0005f) {
				curveState_.SelectSingle(selection.channelIndex, i);
				break;
			}
		}
		UpdateAnimationClipAutoDuration(clip_);
		clipDirty_ = true;
		ApplyPreviewAtCurrentTime(context, true);
	}
}

void AnimationClipTool::DrawGeneratorUI(const EditorToolContext& context) {

	if (selectedTrackIndex_ < 0 || static_cast<int>(clip_.curveTracks.size()) <= selectedTrackIndex_) {
		return;
	}

	AnimationCurveTrack& track = clip_.curveTracks[static_cast<size_t>(selectedTrackIndex_)];
	if (track.channels.empty()) {
		return;
	}

	ImGui::SeparatorText("カーブ生成");

	// DrawClipAssetUIと同じプロパティ行で、生成条件を縦に並べる
	const float reserveRightWidth = ImGui::GetContentRegionAvail().x / 4.0f;
	MyGUI::EnumCombo("生成タイプ", generatorType_, { .reserveRightWidth = reserveRightWidth });

	MyGUI::DragFloat("開始時間", generatorStartTime_, { .dragSpeed = 0.001f,.minValue = 0.0f,
		.maxValue = 10000.0f,.reserveRightWidth = reserveRightWidth });
	MyGUI::DragFloat("終了時間", generatorEndTime_, { .dragSpeed = 0.001f,.minValue = 0.0f,
		.maxValue = 10000.0f,.reserveRightWidth = reserveRightWidth });
	MyGUI::DragFloat("開始値", generatorStartValue_, { .dragSpeed = 0.001f,.minValue = -10000.0f,
		.maxValue = 10000.0f,.reserveRightWidth = reserveRightWidth });
	MyGUI::DragFloat("終了値", generatorEndValue_, { .dragSpeed = 0.001f,.minValue = -10000.0f,
		.maxValue = 10000.0f,.reserveRightWidth = reserveRightWidth });

	if (generatorType_ != GeneratorType::Easing) {

		MyGUI::DragFloat("振幅", generatorAmplitude_, { .dragSpeed = 0.001f,.minValue = -10000.0f,
			.maxValue = 10000.0f,.reserveRightWidth = reserveRightWidth });
		MyGUI::DragFloat("周波数", generatorFrequency_, { .dragSpeed = 0.001f,.minValue = 0.0f,
			.maxValue = 10000.0f,.reserveRightWidth = reserveRightWidth });
		MyGUI::DragFloat("位相", generatorPhase_, { .dragSpeed = 0.001f,.minValue = -10000.0f,
			.maxValue = 10000.0f,.reserveRightWidth = reserveRightWidth });
	} else {

		// イージング選択UIはCore側の共通実装を使う
		DrawEasingComboProperty("イージング", generatorEasingType_, reserveRightWidth);
	}

	MyGUI::DragInt("キー数", generatorSampleCount_, { .dragSpeed = 1.0f,.minValue = 2,.maxValue = 1024 });
	generatorSampleCount_ = (std::max)(generatorSampleCount_, 2);

	MyGUI::EnumCombo("適用先", generatorApplyTo_, { .reserveRightWidth = 0.0f });
	MyGUI::Checkbox("範囲内のキーを置き換える", generatorReplaceKeys_);

	if (!ImGui::Button("生成", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()))) {
		return;
	}

	// 開始/終了の入力順に依存しないよう、生成範囲だけ正規化する
	const float startTime = (std::min)(generatorStartTime_, generatorEndTime_);
	const float endTime = (std::max)(generatorStartTime_, generatorEndTime_);
	const float timeRange = (std::max)(endTime - startTime, 0.001f);

	auto bakeChannel = [&](CurveChannel& channel) {

		if (generatorReplaceKeys_) {
			// 置き換え時は指定範囲内の既存キーだけを消し、範囲外の手作業キーは残す
			channel.keys.erase(std::remove_if(channel.keys.begin(), channel.keys.end(),
				[&](const CurveKey& key) {
					return startTime <= key.time && key.time <= endTime;
				}), channel.keys.end());
		}

		for (int i = 0; i < generatorSampleCount_; ++i) {
			const float normalized = static_cast<float>(i) / static_cast<float>(generatorSampleCount_ - 1);
			const float time = startTime + timeRange * normalized;
			float value = generatorStartValue_;
			// 生成結果は通常編集しやすいよう、まずはLinearキーとして追加する
			if (generatorType_ == GeneratorType::Sin || generatorType_ == GeneratorType::Cos) {
				const float angle = normalized * generatorFrequency_ * 2.0f * std::numbers::pi_v<float> +generatorPhase_;
				const float wave = generatorType_ == GeneratorType::Sin ? std::sin(angle) : std::cos(angle);
				value = generatorStartValue_ + wave * generatorAmplitude_;
			} else {
				const float eased = EasedValue(generatorEasingType_, normalized);
				value = generatorStartValue_ + (generatorEndValue_ - generatorStartValue_) * eased;
			}
			channel.AddKey(time, value, CurveInterpolationMode::Spline);
		}
		};

	if (generatorApplyTo_ == GeneratorApplyTo::SelectedTrack) {
		for (CurveChannel& channel : track.channels) {
			bakeChannel(channel);
		}
	} else {
		uint32_t channelIndex = 0;
		if (!curveState_.selectedKeys.empty()) {
			channelIndex = curveState_.selectedKeys.front().channelIndex;
		}
		if (channelIndex < track.channels.size()) {
			bakeChannel(track.channels[channelIndex]);
		}
	}

	UpdateAutoDurationAndPreview(context);
}

void AnimationClipTool::LoadClipFromSelectedAsset(const EditorToolContext& context) {

	// 読み込み失敗時に前回のClip状態が残らないよう、先にUI状態を初期化する
	clipErrorText_.clear();
	hasClip_ = false;
	clipDirty_ = false;
	loadedClipAssetID_ = {};

	if (!clipAssetID_) {
		clip_ = AnimationClipAsset{};
		return;
	}

	const AssetDatabase* database = context.toolContext.assetDatabase;
	const std::filesystem::path path = database->ResolveFullPath(clipAssetID_);
	if (path.empty()) {
		clipErrorText_ = "AnimationClip asset path was not found.";
		return;
	}

	AnimationClipAsset loaded{};
	if (!LoadAnimationClipAsset(path, loaded)) {
		clipErrorText_ = "Failed to load AnimationClip asset.";
		Logger::Output(LogType::Engine, spdlog::level::err,
			"AnimationClipTool failed to load clip: {}", path.string());
		return;
	}

	clip_ = std::move(loaded);
	clip_.guid = clipAssetID_;
	if (clip_.name.empty()) {
		clip_.name = path.stem().string();
	}
	if (clip_.duration <= 0.0f) {
		clip_.duration = 1.0f;
	}
	for (AnimationCurveTrack& track : clip_.curveTracks) {
		// 旧形式や手編集JSONでも、Runtime評価前にChannel数を現在仕様へ揃える
		NormalizeAnimationTrackChannels(track);
	}

	hasClip_ = true;
	loadedClipAssetID_ = clipAssetID_;
	selectedTrackIndex_ = clip_.curveTracks.empty() ? -1 : 0;
	previewTime_ = (std::clamp)(previewTime_, 0.0f, clip_.duration);
	curveState_.currentTime = previewTime_;
	curveState_.visibleTimeMax = (std::max)(curveState_.visibleTimeMax, clip_.duration);
	curveState_.ClearSelection();
	LoadSelectedTrackEditorView();
	// 読み込み直後も現在時刻のPreviewをTarget Entityへ反映する
	ApplyPreviewAtCurrentTime(context, true);
}

void AnimationClipTool::SaveClipToSelectedAsset(const EditorToolContext& context) {

	// 保存前に表示範囲とAuto DurationをClipへ反映してからJSONへ書き出す
	clipErrorText_.clear();

	if (!clipAssetID_) {
		clipErrorText_ = "ClipData is not set.";
		return;
	}

	const AssetDatabase* database = context.toolContext.assetDatabase;
	const std::filesystem::path path = database->ResolveFullPath(clipAssetID_);
	if (path.empty()) {
		clipErrorText_ = "AnimationClip asset path was not found.";
		return;
	}

	clip_.guid = clipAssetID_;
	if (clip_.name.empty()) {
		clip_.name = path.stem().string();
	}
	StoreSelectedTrackEditorView();
	UpdateAnimationClipAutoDuration(clip_);
	clip_.duration = (std::max)(clip_.duration, 0.01f);
	for (AnimationCurveTrack& track : clip_.curveTracks) {
		// 保存時にもChannel構成を正規化し、次回ロード時のUnknown化を防ぐ
		NormalizeAnimationTrackChannels(track);
	}

	if (!SaveAnimationClipAsset(path, clip_)) {
		clipErrorText_ = "Failed to save AnimationClip asset.";
		Logger::Output(LogType::Engine, spdlog::level::err,
			"AnimationClipTool failed to save clip: {}", path.string());
		return;
	}

	clipDirty_ = false;
}

void AnimationClipTool::RevertClipFromSelectedAsset(const EditorToolContext& context) {

	EndPreviewAndRestore(context);
	LoadClipFromSelectedAsset(context);
}

void AnimationClipTool::AddPropertyTrack(const AnimationPropertyDescriptor& desc,
	ECSWorld& world, const Entity& entity) {

	if (!hasClip_ || !world.IsAlive(entity) || !desc.getValue) {
		return;
	}

	AnimationPropertyValue currentValue{};
	if (!desc.getValue(world, entity, currentValue)) {
		return;
	}

	AnimationCurveTrack track{};
	track.binding.componentName = desc.componentName;
	track.binding.propertyPath = desc.propertyPath;
	track.binding.valueType = desc.valueType;
	track.applyMode = AnimationApplyMode::Override;
	track.visible = true;

	// 追加直後のTrackは現在値をdefaultValueへ写すだけで、キーは空のままにする
	SetupTrackInitialValue(track, currentValue);
	NormalizeAnimationTrackChannels(track);
	selectedTrackIndex_ = static_cast<int>(clip_.curveTracks.size());
	clip_.curveTracks.emplace_back(std::move(track));
	LoadSelectedTrackEditorView();
	clipDirty_ = true;
	curveState_.ClearSelection();
	curveState_.frameSelectionRequest = true;
}

Entity AnimationClipTool::GetTargetEntity(const EditorToolContext& context) const {

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return Entity::Null();
	}

	const Entity entity = world->FindByUUID(targetEntityUUID_);
	return world->IsAlive(entity) ? entity : Entity::Null();
}

AnimationClipDetectedDimension AnimationClipTool::DetectTargetDimension(const EditorToolContext& context) const {

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (!world || !world->IsAlive(entity)) {
		return AnimationClipDetectedDimension::Unknown;
	}

	const bool has2D = world->HasComponent<SpriteRendererComponent>(entity) ||
		world->HasComponent<TextRendererComponent>(entity) ||
		world->HasComponent<OrthographicCameraComponent>(entity);
	const bool has3D = world->HasComponent<MeshRendererComponent>(entity) ||
		world->HasComponent<PerspectiveCameraComponent>(entity);
	// 描画Componentから用途を推定し、Add Propertyの候補を2D/3Dに寄せる
	if (has2D && has3D) {
		return AnimationClipDetectedDimension::Mixed;
	}
	if (has2D) {
		return AnimationClipDetectedDimension::Mode2D;
	}
	return AnimationClipDetectedDimension::Mode3D;
}

AnimationClipEditDimension AnimationClipTool::GetEffectiveEditDimension(const EditorToolContext& context) const {

	if (editDimension_ == AnimationClipEditDimension::Mode2D ||
		editDimension_ == AnimationClipEditDimension::Mode3D) {
		return editDimension_;
	}

	const AnimationClipDetectedDimension detected = DetectTargetDimension(context);
	return detected == AnimationClipDetectedDimension::Mode2D ?
		AnimationClipEditDimension::Mode2D : AnimationClipEditDimension::Mode3D;
}

void AnimationClipTool::NormalizeSelectedTrackIndex() {

	if (clip_.curveTracks.empty()) {
		selectedTrackIndex_ = -1;
		return;
	}
	if (selectedTrackIndex_ < 0 || static_cast<int>(clip_.curveTracks.size()) <= selectedTrackIndex_) {
		selectedTrackIndex_ = 0;
	}
}

void AnimationClipTool::StoreSelectedTrackEditorView() {

	if (editorViewTrackIndex_ < 0 || static_cast<int>(clip_.curveTracks.size()) <= editorViewTrackIndex_) {
		return;
	}

	// Trackを切り替えても、各Propertyごとの表示範囲を保持する
	AnimationTrackEditorView& view = clip_.curveTracks[static_cast<size_t>(editorViewTrackIndex_)].editorView;
	view.timeMin = curveState_.visibleTimeMin;
	view.timeMax = curveState_.visibleTimeMax;
	view.valueMin = curveState_.visibleValueMin;
	view.valueMax = curveState_.visibleValueMax;
}

void AnimationClipTool::LoadSelectedTrackEditorView() {

	if (selectedTrackIndex_ < 0 || static_cast<int>(clip_.curveTracks.size()) <= selectedTrackIndex_) {
		editorViewTrackIndex_ = -1;
		return;
	}

	const AnimationTrackEditorView& view = clip_.curveTracks[static_cast<size_t>(selectedTrackIndex_)].editorView;
	curveState_.visibleTimeMin = view.timeMin;
	curveState_.visibleTimeMax = (std::max)(view.timeMax, view.timeMin + 0.001f);
	curveState_.visibleValueMin = view.valueMin;
	curveState_.visibleValueMax = (std::max)(view.valueMax, view.valueMin + 0.001f);
	// Clip編集では細かい時刻合わせが多いので、既定で1ms単位に吸着させる
	curveState_.snapEnabled = true;
	curveState_.snapInterval = 0.001f;
	editorViewTrackIndex_ = selectedTrackIndex_;
}

void AnimationClipTool::SyncCurveStateTime() {

	if (!hasClip_) {
		previewTime_ = 0.0f;
		curveState_.currentTime = 0.0f;
		return;
	}

	previewTime_ = (std::clamp)(previewTime_, 0.0f, AnimationClipEvaluator::GetPlaybackDuration(clip_));
	curveState_.currentTime = previewTime_;
}

void AnimationClipTool::UpdatePreviewPlayback(const EditorToolContext& context) {

	if (!previewPlaying_ || !hasClip_) {
		return;
	}

	float deltaTime = ImGui::GetIO().DeltaTime;
	if (deltaTime <= 0.0f) {
		deltaTime = context.toolContext.deltaTime;
	}

	// 再生時間を進める
	previewTime_ += deltaTime * previewSpeed_;
	// クリップデータから終了時間を取得
	float playbackDuration = AnimationClipEvaluator::GetPlaybackDuration(clip_);
	if (playbackDuration <= previewTime_) {
		// Loop時はBridge範囲も含めた再生長で折り返す
		if (clip_.loop && 0.0f < clip_.duration) {

			previewTime_ = std::fmod(previewTime_, playbackDuration);
		}
		// ループしない場合はプレビューを停止させる
		else {

			previewTime_ = playbackDuration;
			previewPlaying_ = false;
		}
	}

	SyncCurveStateTime();
	ApplyPreviewAtCurrentTime(context, false);
}

void AnimationClipTool::ApplyPreviewAtCurrentTime(const EditorToolContext& context, bool keepActive) {

	if (!hasClip_) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (!world || !world->IsAlive(entity)) {
		return;
	}

	// ScrubだけでもPreview扱いにして、Stop/Closeで元の値に戻せるようにする
	if (!previewActive_) {
		BeginPreview(context);
	}
	if (!previewActive_) {
		return;
	}

	AnimationClipEvaluator::ApplyClip(*world, entity, clip_, previewTime_, previewBaseValues_);
	if (!keepActive && !previewPlaying_) {
		EndPreviewAndRestore(context);
	}
}

void AnimationClipTool::BeginPreview(const EditorToolContext& context) {

	if (previewActive_) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (!world || !world->IsAlive(entity) || !hasClip_) {
		return;
	}

	// Target Entityへ直接値を書き込むため、開始時の値を先に退避しておく
	CachePreviewBaseValues(*world, entity);
	previewActive_ = true;
}

void AnimationClipTool::EndPreviewAndRestore(const EditorToolContext& context) {

	if (!previewActive_) {
		previewPlaying_ = false;
		return;
	}

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (world && world->IsAlive(entity)) {
		// Toolを閉じた場合も、Clip編集中だけ適用していた値を元へ戻す
		RestorePreviewBaseValues(*world, entity);
	}

	previewBaseValues_.clear();
	previewActive_ = false;
	previewPlaying_ = false;
}

void AnimationClipTool::CachePreviewBaseValues(ECSWorld& world, const Entity& entity) {

	previewBaseValues_.clear();
	for (const AnimationCurveTrack& track : clip_.curveTracks) {

		const std::optional<AnimationPropertyDescriptor> desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
			world, entity, track.binding.componentName, track.binding.propertyPath, track.binding.valueType);
		if (!desc || !desc->getValue || !desc->hasComponent || !desc->hasComponent(world, entity)) {
			continue;
		}

		// Clipに含まれるPropertyだけ退避し、無関係なComponent値は触らない
		AnimationPreviewBaseValue baseValue{};
		baseValue.binding = track.binding;
		// material override未設定などは値が無いので、復元時に除去できるよう有無を記録する
		baseValue.present = !desc->hasValue || desc->hasValue(world, entity);
		if (desc->getValue(world, entity, baseValue.value)) {
			previewBaseValues_.emplace_back(std::move(baseValue));
		}
	}

	// 向き相対クリップは基準の位置/回転が必須なので、未アニメでも捕捉しておく
	if (clip_.relativeTransform) {

		const auto ensureTransformBase = [&](const char* propertyPath, AnimationValueType valueType) {

			for (const AnimationPreviewBaseValue& base : previewBaseValues_) {
				if (base.binding.componentName == "Transform" && base.binding.propertyPath == propertyPath) {
					return;
				}
			}
			const std::optional<AnimationPropertyDescriptor> desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
				world, entity, "Transform", propertyPath, valueType);
			if (!desc || !desc->getValue || !desc->hasComponent || !desc->hasComponent(world, entity)) {
				return;
			}
			AnimationPreviewBaseValue base{};
			base.binding.componentName = "Transform";
			base.binding.propertyPath = propertyPath;
			base.binding.valueType = valueType;
			base.present = !desc->hasValue || desc->hasValue(world, entity);
			if (desc->getValue(world, entity, base.value)) {
				previewBaseValues_.emplace_back(std::move(base));
			}
		};
		ensureTransformBase("localPos", AnimationValueType::Vector3);
		ensureTransformBase("localRotation", AnimationValueType::Quaternion);
		ensureTransformBase("localPos2D", AnimationValueType::Vector2);
		ensureTransformBase("localRotationZ", AnimationValueType::Float);
	}
}

void AnimationClipTool::RestorePreviewBaseValues(ECSWorld& world, const Entity& entity) {

	for (const AnimationPreviewBaseValue& baseValue : previewBaseValues_) {

		const std::optional<AnimationPropertyDescriptor> desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
			world, entity, baseValue.binding.componentName, baseValue.binding.propertyPath, baseValue.binding.valueType);
		if (!desc || !desc->hasComponent || !desc->hasComponent(world, entity)) {
			continue;
		}

		// Preview前に値が有ったものは戻し、無かったものは除去して既定の見た目へ戻す
		if (baseValue.present) {
			if (desc->setValue) {
				desc->setValue(world, entity, baseValue.value);
			}
		} else if (desc->clearValue) {
			desc->clearValue(world, entity);
		}
	}
}

void AnimationClipTool::AddKeyToChannel(AnimationCurveTrack& track, uint32_t channelIndex, float time) {

	if (track.channels.size() <= channelIndex) {
		return;
	}

	CurveChannel& channel = track.channels[channelIndex];
	const float value = channel.Evaluate(time);
	constexpr float kSameTimeEpsilon = 0.0005f;
	for (CurveKey& key : channel.keys) {
		if (std::abs(key.time - time) <= kSameTimeEpsilon) {
			// ほぼ同時刻のキーは増やさず、現在の評価値で上書きする
			key.time = time;
			key.value = value;
			return;
		}
	}

	const uint32_t addedIndex = channel.AddKey(time, value, CurveInterpolationMode::Spline);
	if (IsQuaternionAxisAngleTrack(track) && channelIndex == 0u) {
		CurveQuaternionAxisKey axisKey = QuaternionAxisKeyUtility::MakeDefault();
		if (!track.quaternionAxisKeys.empty()) {
			const uint32_t sourceIndex = (std::min)(
				addedIndex,
				static_cast<uint32_t>(track.quaternionAxisKeys.size() - 1));
			axisKey = track.quaternionAxisKeys[sourceIndex];
		}
		const uint32_t insertIndex = (std::min)(addedIndex, static_cast<uint32_t>(track.quaternionAxisKeys.size()));
		track.quaternionAxisKeys.insert(track.quaternionAxisKeys.begin() + insertIndex, axisKey);
		SortQuaternionAxisKeys(track);
	}
}

void AnimationClipTool::UpdateAutoDurationAndPreview(const EditorToolContext& context) {

	// キー追加/生成後はDuration、現在時刻、SceneView Previewをまとめて同期する
	UpdateAnimationClipAutoDuration(clip_);
	previewTime_ = (std::clamp)(previewTime_, 0.0f, AnimationClipEvaluator::GetPlaybackDuration(clip_));
	curveState_.currentTime = previewTime_;
	clipDirty_ = true;
	ApplyPreviewAtCurrentTime(context, true);
}
