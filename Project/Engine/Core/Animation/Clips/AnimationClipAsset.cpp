#include "AnimationClipAsset.h"

//============================================================================
//	include
//============================================================================
#include "AnimationChannelUtility.h"
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>

// c++
#include <algorithm>
#include <array>
#include <fstream>

using namespace Engine::AnimationChannelUtility;

//============================================================================
//	AnimationClipAsset functions
//============================================================================
namespace {

	Engine::CurveChannel MakeChannel(std::string_view name, float defaultValue) {

		Engine::CurveChannel channel{};
		channel.name = std::string(name);
		channel.displayColor = GetChannelColor(name);
		channel.defaultValue = defaultValue;
		return channel;
	}

	bool IsQuaternionAxisAngleChannels(const std::vector<Engine::CurveChannel>& channels) {

		return channels.size() == 2 && channels[0].name == "Axis" && channels[1].name == "Angle";
	}

}

std::string Engine::ToString(AnimationValueType type) {

	return std::string(EnumAdapter<AnimationValueType>::ToString(type));
}

bool Engine::TryParseAnimationValueType(std::string_view text, AnimationValueType& out) {

	const std::optional<AnimationValueType> parsed = EnumAdapter<AnimationValueType>::FromString(text);
	if (!parsed) {
		return false;
	}
	out = *parsed;
	return true;
}

std::string Engine::ToString(AnimationApplyMode mode) {

	return std::string(EnumAdapter<AnimationApplyMode>::ToString(mode));
}

bool Engine::TryParseAnimationApplyMode(std::string_view text, AnimationApplyMode& out) {

	const std::optional<AnimationApplyMode> parsed = EnumAdapter<AnimationApplyMode>::FromString(text);
	if (!parsed) {
		return false;
	}
	out = *parsed;
	return true;
}

std::string Engine::ToString(QuaternionMultiplyOrder order) {

	return std::string(EnumAdapter<QuaternionMultiplyOrder>::ToString(order));
}

bool Engine::TryParseQuaternionMultiplyOrder(std::string_view text, QuaternionMultiplyOrder& out) {

	const std::optional<QuaternionMultiplyOrder> parsed = EnumAdapter<QuaternionMultiplyOrder>::FromString(text);
	if (!parsed) {
		return false;
	}
	out = *parsed;
	return true;
}

std::string Engine::ToString(CurveInterpolationMode mode) {

	return std::string(EnumAdapter<CurveInterpolationMode>::ToString(mode));
}

bool Engine::TryParseCurveInterpolationMode(std::string_view text, CurveInterpolationMode& out) {

	const std::optional<CurveInterpolationMode> parsed = EnumAdapter<CurveInterpolationMode>::FromString(text);
	if (!parsed) {
		return false;
	}
	out = *parsed;
	return true;
}

uint32_t Engine::GetAnimationValueTypeChannelCount(AnimationValueType type) {

	switch (type) {
	case AnimationValueType::Float:
		return 1;
	case AnimationValueType::Vector2:
		return 2;
	case AnimationValueType::Vector3:
	case AnimationValueType::Color3:
		return 3;
	case AnimationValueType::Vector4:
	case AnimationValueType::Color4:
	case AnimationValueType::Quaternion:
		return 4;
	default:
		break;
	}
	return 1;
}

std::vector<Engine::CurveChannel> Engine::MakeDefaultAnimationChannels(AnimationValueType type) {

	// キーは作らず、型ごとのチャンネル名とdefaultValueだけを用意する
	std::vector<CurveChannel> channels{};

	switch (type) {
	case AnimationValueType::Vector2:
		channels = { MakeChannel("X", 0.0f), MakeChannel("Y", 0.0f) };
		break;
	case AnimationValueType::Vector3:
		channels = { MakeChannel("X", 0.0f), MakeChannel("Y", 0.0f), MakeChannel("Z", 0.0f) };
		break;
	case AnimationValueType::Vector4:
		channels = { MakeChannel("X", 0.0f), MakeChannel("Y", 0.0f), MakeChannel("Z", 0.0f), MakeChannel("W", 0.0f) };
		break;
	case AnimationValueType::Color3:
		channels = { MakeChannel("R", 1.0f), MakeChannel("G", 1.0f), MakeChannel("B", 1.0f) };
		break;
	case AnimationValueType::Color4:
		channels = { MakeChannel("R", 1.0f), MakeChannel("G", 1.0f), MakeChannel("B", 1.0f), MakeChannel("A", 1.0f) };
		break;
	case AnimationValueType::Quaternion:
		channels = { MakeChannel("Axis", 0.0f), MakeChannel("Angle", 0.0f) };
		break;
	case AnimationValueType::Float:
	default:
		channels = { MakeChannel("Value", 0.0f) };
		break;
	}
	return channels;
}

void Engine::NormalizeAnimationTrackChannels(AnimationCurveTrack& track) {

	// JSONの手編集や古い形式でチャンネル数がずれた場合でも、ツール側で落ちない形へ補正する
	if (track.binding.valueType == AnimationValueType::Quaternion) {

		if (IsQuaternionAxisAngleChannels(track.channels)) {
			track.channels[0].displayColor = GetChannelColor(track.channels[0].name);
			track.channels[1].displayColor = GetChannelColor(track.channels[1].name);
			track.channels[0].SortKeys();
			track.channels[1].SortKeys();
			while (track.quaternionAxisKeys.size() < track.channels[0].keys.size()) {
				track.quaternionAxisKeys.emplace_back(Engine::QuaternionAxisKeyUtility::MakeDefault());
			}
			if (track.channels[0].keys.size() < track.quaternionAxisKeys.size()) {
				track.quaternionAxisKeys.resize(track.channels[0].keys.size());
			}
			return;
		}

		track.channels = MakeDefaultAnimationChannels(track.binding.valueType);
		track.quaternionAxisKeys.clear();
		return;
	}

	const uint32_t expectedCount = GetAnimationValueTypeChannelCount(track.binding.valueType);
	std::vector<CurveChannel> defaults = MakeDefaultAnimationChannels(track.binding.valueType);

	if (track.channels.size() < expectedCount) {
		for (size_t i = track.channels.size(); i < expectedCount; ++i) {
			track.channels.emplace_back(defaults[i]);
		}
	}
	if (expectedCount < track.channels.size()) {
		track.channels.resize(expectedCount);
	}

	for (uint32_t i = 0; i < expectedCount; ++i) {
		// name?空だとCurveEditorのチャンネル一覧が読みにくくなるため、既定名を補う
		if (track.channels[i].name.empty()) {
			track.channels[i].name = defaults[i].name;
		}
		track.channels[i].displayColor = GetChannelColor(track.channels[i].name);
		track.channels[i].SortKeys();
	}
}

void Engine::UpdateAnimationClipAutoDuration(AnimationClipAsset& clip) {

	if (!clip.autoDuration) {
		return;
	}

	// 全Track/Channelの最大キー時刻をClip長にする
	bool hasKey = false;
	float maxTime = 0.0f;
	for (const AnimationCurveTrack& track : clip.curveTracks) {
		for (const CurveChannel& channel : track.channels) {
			for (const CurveKey& key : channel.keys) {
				maxTime = (std::max)(maxTime, key.time);
				hasKey = true;
			}
		}
	}

	if (hasKey) {
		clip.duration = (std::max)(maxTime, 0.001f);
	}
}
