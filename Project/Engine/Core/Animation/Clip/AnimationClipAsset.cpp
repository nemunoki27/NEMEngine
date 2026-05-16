#include "AnimationClipAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Logger/Logger.h>
#include <Engine/Core/UUID/UUID.h>

// c++
#include <array>
#include <fstream>

//============================================================================
//	AnimationClipAsset functions
//============================================================================

namespace {

	struct ValueTypeName {

		Engine::AnimationValueType type = Engine::AnimationValueType::Float;
		std::string_view name;
	};

	struct ApplyModeName {

		Engine::AnimationApplyMode mode = Engine::AnimationApplyMode::Override;
		std::string_view name;
	};

	struct InterpolationName {

		Engine::CurveInterpolationMode mode = Engine::CurveInterpolationMode::Linear;
		std::string_view name;
	};

	constexpr std::array<ValueTypeName, 7> kValueTypeNames{ {
		{ Engine::AnimationValueType::Float, "Float" },
		{ Engine::AnimationValueType::Vector2, "Vector2" },
		{ Engine::AnimationValueType::Vector3, "Vector3" },
		{ Engine::AnimationValueType::Vector4, "Vector4" },
		{ Engine::AnimationValueType::Color3, "Color3" },
		{ Engine::AnimationValueType::Color4, "Color4" },
		{ Engine::AnimationValueType::Quaternion, "Quaternion" },
	} };

	constexpr std::array<ApplyModeName, 3> kApplyModeNames{ {
		{ Engine::AnimationApplyMode::Override, "Override" },
		{ Engine::AnimationApplyMode::Add, "Add" },
		{ Engine::AnimationApplyMode::Multiply, "Multiply" },
	} };

	constexpr std::array<InterpolationName, 3> kInterpolationNames{ {
		{ Engine::CurveInterpolationMode::Constant, "Constant" },
		{ Engine::CurveInterpolationMode::Linear, "Linear" },
		{ Engine::CurveInterpolationMode::Cubic, "Cubic" },
	} };

	Engine::Color4 GetChannelColor(std::string_view name) {

		if (name == "X" || name == "R") {
			return Engine::Color4::Red();
		}
		if (name == "Y" || name == "G") {
			return Engine::Color4::Green();
		}
		if (name == "Z" || name == "B") {
			return Engine::Color4::Blue();
		}
		if (name == "W" || name == "A") {
			return Engine::Color4(0.85f, 0.85f, 0.85f, 1.0f);
		}
		return Engine::Color4::White();
	}

	Engine::CurveChannel MakeChannel(std::string_view name, float defaultValue) {

		Engine::CurveChannel channel{};
		channel.name = std::string(name);
		channel.displayColor = GetChannelColor(name);
		channel.defaultValue = defaultValue;
		return channel;
	}

	void ToJson(Engine::CurveKey key, nlohmann::json& out) {

		out = nlohmann::json::object();
		out["time"] = key.time;
		out["value"] = key.value;
		out["interpolation"] = Engine::ToString(key.interpolation);
	}

	Engine::CurveKey ParseCurveKey(const nlohmann::json& in) {

		Engine::CurveKey key{};
		key.time = in.value("time", key.time);
		key.value = in.value("value", key.value);

		Engine::CurveInterpolationMode interpolation = key.interpolation;
		if (Engine::TryParseCurveInterpolationMode(in.value("interpolation", "Linear"), interpolation)) {
			key.interpolation = interpolation;
		}
		return key;
	}

	void ToJson(const Engine::CurveChannel& channel, nlohmann::json& out) {

		out = nlohmann::json::object();
		out["name"] = channel.name;
		out["defaultValue"] = channel.defaultValue;
		out["keys"] = nlohmann::json::array();
		for (const Engine::CurveKey& key : channel.keys) {

			nlohmann::json keyJson;
			ToJson(key, keyJson);
			out["keys"].push_back(keyJson);
		}
	}

	Engine::CurveChannel ParseCurveChannel(const nlohmann::json& in) {

		Engine::CurveChannel channel{};
		channel.name = in.value("name", channel.name);
		channel.displayColor = GetChannelColor(channel.name);
		channel.defaultValue = in.value("defaultValue", channel.defaultValue);

		if (const auto it = in.find("keys"); it != in.end() && it->is_array()) {
			for (const nlohmann::json& keyJson : *it) {
				channel.keys.emplace_back(ParseCurveKey(keyJson));
			}
			channel.SortKeys();
		}
		return channel;
	}
}

std::string Engine::ToString(AnimationValueType type) {

	for (const auto& item : kValueTypeNames) {
		if (item.type == type) {
			return std::string(item.name);
		}
	}
	return "Float";
}

bool Engine::TryParseAnimationValueType(std::string_view text, AnimationValueType& out) {

	for (const auto& item : kValueTypeNames) {
		if (item.name == text) {
			out = item.type;
			return true;
		}
	}
	return false;
}

std::string Engine::ToString(AnimationApplyMode mode) {

	for (const auto& item : kApplyModeNames) {
		if (item.mode == mode) {
			return std::string(item.name);
		}
	}
	return "Override";
}

bool Engine::TryParseAnimationApplyMode(std::string_view text, AnimationApplyMode& out) {

	for (const auto& item : kApplyModeNames) {
		if (item.name == text) {
			out = item.mode;
			return true;
		}
	}
	return false;
}

std::string Engine::ToString(CurveInterpolationMode mode) {

	for (const auto& item : kInterpolationNames) {
		if (item.mode == mode) {
			return std::string(item.name);
		}
	}
	return "Linear";
}

bool Engine::TryParseCurveInterpolationMode(std::string_view text, CurveInterpolationMode& out) {

	for (const auto& item : kInterpolationNames) {
		if (item.name == text) {
			out = item.mode;
			return true;
		}
	}
	return false;
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
		channels = { MakeChannel("X", 0.0f), MakeChannel("Y", 0.0f), MakeChannel("Z", 0.0f), MakeChannel("W", 1.0f) };
		break;
	case AnimationValueType::Float:
	default:
		channels = { MakeChannel("Value", 0.0f) };
		break;
	}
	return channels;
}

void Engine::NormalizeAnimationTrackChannels(AnimationCurveTrack& track) {

	// JSONの手編集や古い形式でチャンネル数がずれた場合でも、ツール側で落ちない形へ補正する。
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
		// nameが空だとCurveEditorのチャンネル一覧が読みにくくなるため、既定名を補う。
		if (track.channels[i].name.empty()) {
			track.channels[i].name = defaults[i].name;
		}
		track.channels[i].displayColor = GetChannelColor(track.channels[i].name);
		track.channels[i].SortKeys();
	}
}

bool Engine::LoadAnimationClipAsset(const std::filesystem::path& path, AnimationClipAsset& outClip) {

	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		return false;
	}

	try {
		nlohmann::json data{};
		file >> data;
		outClip = data.get<AnimationClipAsset>();
		return true;
	} catch (const std::exception& e) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"Failed to load AnimationClipAsset. path: {}, error: {}", path.string(), e.what());
		return false;
	}
}

bool Engine::SaveAnimationClipAsset(const std::filesystem::path& path, const AnimationClipAsset& clip) {

	try {
		std::filesystem::create_directories(path.parent_path());

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file.is_open()) {
			return false;
		}

		nlohmann::json data = clip;
		file << data.dump(2);
		return true;
	} catch (const std::exception& e) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"Failed to save AnimationClipAsset. path: {}, error: {}", path.string(), e.what());
		return false;
	}
}

void Engine::to_json(nlohmann::json& out, const AnimationPropertyBinding& binding) {

	out = nlohmann::json::object();
	out["component"] = binding.componentName;
	out["property"] = binding.propertyPath;
	out["type"] = ToString(binding.valueType);
}

void Engine::from_json(const nlohmann::json& in, AnimationPropertyBinding& binding) {

	binding.componentName = in.value("component", in.value("componentName", binding.componentName));
	binding.propertyPath = in.value("property", in.value("propertyPath", binding.propertyPath));

	AnimationValueType type = binding.valueType;
	if (TryParseAnimationValueType(in.value("type", "Float"), type)) {
		binding.valueType = type;
	}
}

void Engine::to_json(nlohmann::json& out, const AnimationCurveTrack& track) {

	out = nlohmann::json::object();
	out["component"] = track.binding.componentName;
	out["property"] = track.binding.propertyPath;
	out["type"] = ToString(track.binding.valueType);
	out["applyMode"] = ToString(track.applyMode);
	out["visible"] = track.visible;
	out["channels"] = nlohmann::json::array();

	for (const CurveChannel& channel : track.channels) {
		nlohmann::json channelJson;
		ToJson(channel, channelJson);
		out["channels"].push_back(channelJson);
	}
}

void Engine::from_json(const nlohmann::json& in, AnimationCurveTrack& track) {

	from_json(in, track.binding);

	AnimationApplyMode applyMode = track.applyMode;
	if (TryParseAnimationApplyMode(in.value("applyMode", "Override"), applyMode)) {
		track.applyMode = applyMode;
	}
	track.visible = in.value("visible", track.visible);

	track.channels.clear();
	if (const auto it = in.find("channels"); it != in.end() && it->is_array()) {
		for (const nlohmann::json& channelJson : *it) {
			track.channels.emplace_back(ParseCurveChannel(channelJson));
		}
	}
	NormalizeAnimationTrackChannels(track);
}

void Engine::to_json(nlohmann::json& out, const AnimationClipAsset& clip) {

	out = nlohmann::json::object();
	out["guid"] = ToString(clip.guid);
	out["name"] = clip.name;
	out["duration"] = clip.duration;
	out["loop"] = clip.loop;
	out["curveTracks"] = clip.curveTracks;
	out["eventTracks"] = nlohmann::json::array();
}

void Engine::from_json(const nlohmann::json& in, AnimationClipAsset& clip) {

	clip.guid = FromString16Hex(in.value("guid", ""));
	clip.name = in.value("name", clip.name);
	clip.duration = in.value("duration", clip.duration);
	if (clip.duration <= 0.0f) {
		clip.duration = 1.0f;
	}
	clip.loop = in.value("loop", clip.loop);

	clip.curveTracks.clear();
	if (const auto it = in.find("curveTracks"); it != in.end() && it->is_array()) {
		for (const nlohmann::json& trackJson : *it) {
			clip.curveTracks.emplace_back(trackJson.get<AnimationCurveTrack>());
		}
	}

	// 初期実装ではEvent Trackは編集しない。キーが無くても保存時に空配列として維持する。
	clip.eventTracks.clear();
}
