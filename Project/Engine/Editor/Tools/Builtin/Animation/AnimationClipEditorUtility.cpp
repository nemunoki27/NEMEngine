#include "AnimationClipEditorUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>

// c++
#include <algorithm>
#include <charconv>
#include <cmath>
#include <string_view>

namespace Engine::AnimationClipEditorUtility {

	bool ApproxEqualValue(const AnimationPropertyValue& lhs, const AnimationPropertyValue& rhs) {

		// 同じ値型の成分を許容誤差内で比較する
		if (lhs.index() != rhs.index()) {
			return false;
		}
		constexpr float kEpsilon = 1.0e-4f;
		const auto nearly = [](float a, float b) { return std::fabs(a - b) <= kEpsilon; };

		if (const float* v = std::get_if<float>(&lhs)) {
			return nearly(*v, std::get<float>(rhs));
		}
		if (const Vector2* v = std::get_if<Vector2>(&lhs)) {
			const Vector2& o = std::get<Vector2>(rhs);
			return nearly(v->x, o.x) && nearly(v->y, o.y);
		}
		if (const Vector3* v = std::get_if<Vector3>(&lhs)) {
			const Vector3& o = std::get<Vector3>(rhs);
			return nearly(v->x, o.x) && nearly(v->y, o.y) && nearly(v->z, o.z);
		}
		if (const Vector4* v = std::get_if<Vector4>(&lhs)) {
			const Vector4& o = std::get<Vector4>(rhs);
			return nearly(v->x, o.x) && nearly(v->y, o.y) && nearly(v->z, o.z) && nearly(v->w, o.w);
		}
		if (const Color3* v = std::get_if<Color3>(&lhs)) {
			const Color3& o = std::get<Color3>(rhs);
			return nearly(v->r, o.r) && nearly(v->g, o.g) && nearly(v->b, o.b);
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

	AnimationPropertyValue MergeEditedBaseValue(
		const AnimationCurveTrack& track, const AnimationPropertyValue& current, const AnimationPropertyValue& base) {

		if (current.index() != base.index()) {
			return current;
		}
		// 成分に対応するChannelのキーを調べる
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
			// 回転軸と角度にキーがあれば基準値を保つ
			const bool anyKeyed = std::any_of(track.channels.begin(), track.channels.end(),
				[](const CurveChannel& channel) { return !channel.keys.empty(); });
			return anyKeyed ? base : current;
		}
		return current;
	}

	const char* ApplyModeLabel(AnimationApplyMode mode) {
		switch (mode) {
		case AnimationApplyMode::Override:
			return "上書き";
		case AnimationApplyMode::Add:
			return "加算";
		case AnimationApplyMode::Multiply:
			return "乗算";
		}
		return "上書き";
	}

	std::vector<CurveBakeTarget> BuildBakeTargets(const AnimationCurveTrack& track) {

		const uint32_t channelCount = static_cast<uint32_t>(track.channels.size());
		std::vector<CurveBakeTarget> options{};
		switch (track.binding.valueType) {
		case AnimationValueType::Vector2:
			if (channelCount >= 2) {
				options = {{"X", {0u}}, {"Y", {1u}}};
			}
			break;
		case AnimationValueType::Vector3:
			if (channelCount >= 3) {
				options = {{"X", {0u}}, {"Y", {1u}}, {"Z", {2u}}};
			}
			break;
		case AnimationValueType::Vector4:
			if (channelCount >= 4) {
				options = {{"X", {0u}}, {"Y", {1u}}, {"Z", {2u}}, {"W", {3u}}};
			}
			break;
		case AnimationValueType::Quaternion:
			// Quaternionは角度だけを生成対象にする
			if (channelCount >= 2) {
				options = {{"Angle", {1u}}};
			}
			break;
		case AnimationValueType::Color3:
			if (channelCount >= 3) {
				options = {{"RGB", {0u, 1u, 2u}}};
			}
			break;
		case AnimationValueType::Color4:
			if (channelCount >= 4) {
				options = {{"RGB", {0u, 1u, 2u}}, {"Alpha", {3u}}};
			}
			break;
		case AnimationValueType::Float:
		default:
			if (channelCount >= 1) {
				options = {{"値", {0u}}};
			}
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
		// 一括指定とSubMesh名を表示へ反映する
		if (path.rfind("allMesh", 0) == 0) {
			return "全メッシュ" + path.substr(std::string_view("allMesh").size());
		}
		if (path.rfind("subMeshes[", 0) == 0 && world && world->IsAlive(entity)) {

			const size_t rb = path.find(']');
			if (rb != std::string::npos && rb > 10) {

				uint32_t index = 0;
				// 範囲外の添字を別のSubMeshへ対応付けない
				const char* begin = path.data() + 10;
				const char* end = path.data() + rb;
				const auto result = std::from_chars(begin, end, index);
				if (result.ec == std::errc{} && result.ptr == end) {
					const std::span<const SubMeshMaterial> subMeshes = GetMeshSubMeshes(*world, entity);
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

		return propertyPath == "localPos2D" || propertyPath == "localRotationZ" || propertyPath == "localScale2D";
	}

	bool Is3DTransformProperty(std::string_view propertyPath) {

		return propertyPath == "localPos" || propertyPath == "localRotation" || propertyPath == "localScale";
	}

	void FillChannel(CurveChannel& channel, float value) {

		channel.defaultValue = value;
		channel.keys.clear();
	}

	void SetupTrackInitialValue(AnimationCurveTrack& track, const AnimationPropertyValue& value) {

		track.channels = MakeDefaultAnimationChannels(track.binding.valueType);

		// キーを作らず現在値を既定値へ取り込む
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

		if (track.binding.valueType != AnimationValueType::Color3 && track.binding.valueType != AnimationValueType::Color4) {
			return false;
		}
		// AlphaのキーはRGBと別に編集する
		if (track.binding.valueType == AnimationValueType::Color4 && selectedChannelIndex == 3u) {
			return false;
		}

		constexpr uint32_t kRgbChannelCount = 3u;
		return kRgbChannelCount <= track.channels.size();
	}
}
