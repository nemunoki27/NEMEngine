#include "AnimationClipEditorUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ImGui/ImGuiEnum.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>

// imgui
// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <string_view>
#include <unordered_map>

#include <imgui.h>

namespace Engine::AnimationClipEditorUtility {

	bool ApproxEqualValue(const AnimationPropertyValue& lhs, const AnimationPropertyValue& rhs) {

		if (lhs.index() != rhs.index()) {
			return false;
		}
		constexpr float kEpsilon = 1.0e-4f;
		const auto nearly = [](float a, float b) { return std::fabs(a - b) <= kEpsilon; };

		if (const float* v = std::get_if<float>(&lhs)) { return nearly(*v, std::get<float>(rhs)); }
		if (const Vector2* v = std::get_if<Vector2>(&lhs)) {
			const Vector2& o = std::get<Vector2>(rhs); return nearly(v->x, o.x) && nearly(v->y, o.y);
		}
		if (const Vector3* v = std::get_if<Vector3>(&lhs)) {
			const Vector3& o = std::get<Vector3>(rhs); return nearly(v->x, o.x) && nearly(v->y, o.y) && nearly(v->z, o.z);
		}
		if (const Vector4* v = std::get_if<Vector4>(&lhs)) {
			const Vector4& o = std::get<Vector4>(rhs);
			return nearly(v->x, o.x) && nearly(v->y, o.y) && nearly(v->z, o.z) && nearly(v->w, o.w);
		}
		if (const Color3* v = std::get_if<Color3>(&lhs)) {
			const Color3& o = std::get<Color3>(rhs); return nearly(v->r, o.r) && nearly(v->g, o.g) && nearly(v->b, o.b);
		}
		if (const Color4* v = std::get_if<Color4>(&lhs)) {
			const Color4& o = std::get<Color4>(rhs);
			return nearly(v->r, o.r) && nearly(v->g, o.g) && nearly(v->b, o.b) && nearly(v->a, o.a);
		}
		if (const Quaternion* v = std::get_if<Quaternion>(&lhs)) {
			const Quaternion& o = std::get<Quaternion>(rhs);
			return nearly(v->x, o.x) && nearly(v->y, o.y) && nearly(v->z, o.z) && nearly(v->w, o.w);
		}
		return true;
	}

	AnimationPropertyValue MergeEditedBaseValue(const AnimationCurveTrack& track,
		const AnimationPropertyValue& current, const AnimationPropertyValue& base) {

		if (current.index() != base.index()) {
			return current;
		}
		// 成分indexに対応するチャネルがキーを持つ(=アニメされる)か
		const auto keyed = [&](size_t channelIndex) {
			return channelIndex < track.channels.size() && !track.channels[channelIndex].keys.empty();
			};

		if (std::get_if<float>(&current)) {
			return keyed(0) ? base : current;
		}
		if (const Vector2* c = std::get_if<Vector2>(&current)) {
			const Vector2& b = std::get<Vector2>(base);
			return Vector2(keyed(0) ? b.x : c->x, keyed(1) ? b.y : c->y);
		}
		if (const Vector3* c = std::get_if<Vector3>(&current)) {
			const Vector3& b = std::get<Vector3>(base);
			return Vector3(keyed(0) ? b.x : c->x, keyed(1) ? b.y : c->y, keyed(2) ? b.z : c->z);
		}
		if (const Vector4* c = std::get_if<Vector4>(&current)) {
			const Vector4& b = std::get<Vector4>(base);
			return Vector4(keyed(0) ? b.x : c->x, keyed(1) ? b.y : c->y, keyed(2) ? b.z : c->z, keyed(3) ? b.w : c->w);
		}
		if (const Color3* c = std::get_if<Color3>(&current)) {
			const Color3& b = std::get<Color3>(base);
			return Color3(keyed(0) ? b.r : c->r, keyed(1) ? b.g : c->g, keyed(2) ? b.b : c->b);
		}
		if (const Color4* c = std::get_if<Color4>(&current)) {
			const Color4& b = std::get<Color4>(base);
			return Color4(keyed(0) ? b.r : c->r, keyed(1) ? b.g : c->g, keyed(2) ? b.b : c->b, keyed(3) ? b.a : c->a);
		}
		if (std::get_if<Quaternion>(&current)) {
			// Quaternionはaxis/angleチャネルで成分対応しないため、1つでもキーがあればbaseを保つ
			const bool anyKeyed = std::any_of(track.channels.begin(), track.channels.end(),
				[](const CurveChannel& channel) { return !channel.keys.empty(); });
			return anyKeyed ? base : current;
		}
		return current;
	}

	const char* ApplyModeLabel(AnimationApplyMode mode) {
		switch (mode) {
		case AnimationApplyMode::Override: return "上書き";
		case AnimationApplyMode::Add:      return "加算";
		case AnimationApplyMode::Multiply: return "乗算";
		}
		return "上書き";
	}

	std::vector<CurveBakeTarget> BuildBakeTargets(const AnimationCurveTrack& track) {

		const uint32_t channelCount = static_cast<uint32_t>(track.channels.size());
		std::vector<CurveBakeTarget> options{};
		switch (track.binding.valueType) {
		case AnimationValueType::Vector2:
			if (channelCount >= 2) { options = { { "X", { 0u } }, { "Y", { 1u } } }; }
			break;
		case AnimationValueType::Vector3:
			if (channelCount >= 3) { options = { { "X", { 0u } }, { "Y", { 1u } }, { "Z", { 2u } } }; }
			break;
		case AnimationValueType::Vector4:
			if (channelCount >= 4) { options = { { "X", { 0u } }, { "Y", { 1u } }, { "Z", { 2u } }, { "W", { 3u } } }; }
			break;
		case AnimationValueType::Quaternion:
			// QuaternionはAxis(ch0)とAngle(ch1)のうちAngleだけをベイク対象にする
			if (channelCount >= 2) { options = { { "Angle", { 1u } } }; }
			break;
		case AnimationValueType::Color3:
			if (channelCount >= 3) { options = { { "RGB", { 0u, 1u, 2u } } }; }
			break;
		case AnimationValueType::Color4:
			if (channelCount >= 4) { options = { { "RGB", { 0u, 1u, 2u } }, { "Alpha", { 3u } } }; }
			break;
		case AnimationValueType::Float:
		default:
			if (channelCount >= 1) { options = { { "値", { 0u } } }; }
			break;
		}
		return options;
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
					const std::span<const SubMeshMaterial> subMeshes =
						GetMeshSubMeshes(*world, entity);
					if (index < subMeshes.size() && !subMeshes[index].name.empty()) {
						return subMeshes[index].name + path.substr(rb + 1);
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
