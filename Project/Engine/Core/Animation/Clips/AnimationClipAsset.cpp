#include "AnimationClipAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>

// c++
#include <algorithm>
#include <array>
#include <fstream>

//============================================================================
//	AnimationClipAsset functions
//============================================================================
namespace {

	Engine::Color4 GetChannelColor(std::string_view name) {

		// X/Y/Z/WとR/G/B/Aを同じ色規則にして、Vector/Colorで見た目を揃える
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
		if (name == "Axis") {
			return Engine::Color4(0.95f, 0.85f, 0.20f, 1.0f);
		}
		if (name == "Angle") {
			return Engine::Color4(0.25f, 0.65f, 1.0f, 1.0f);
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

	bool IsQuaternionAxisAngleChannels(const std::vector<Engine::CurveChannel>& channels) {

		return channels.size() == 2 && channels[0].name == "Axis" && channels[1].name == "Angle";
	}

	void ToJson(Engine::CurveKey key, nlohmann::json& out) {

		// tangentはBezier以外でも保存しておき補間を切り替えた時に値を戻せるようにする
		out = nlohmann::json::object();
		out["time"] = key.time;
		out["value"] = key.value;
		out["interpolation"] = Engine::ToString(key.interpolation);
		out["inTangent"] = key.inTangent.ToJson();
		out["outTangent"] = key.outTangent.ToJson();
	}

	Engine::CurveKey ParseCurveKey(const nlohmann::json& in) {

		Engine::CurveKey key{};
		key.time = in.value("time", key.time);
		key.value = in.value("value", key.value);

		// 古い"Cubic"表記はTryParse側でSplineへ寄せる
		Engine::CurveInterpolationMode interpolation = key.interpolation;
		if (Engine::TryParseCurveInterpolationMode(in.value("interpolation", "Linear"), interpolation)) {
			key.interpolation = interpolation;
		}
		if (const auto it = in.find("inTangent"); it != in.end()) {
			key.inTangent = Engine::Vector2::FromJson(*it);
		}
		if (const auto it = in.find("outTangent"); it != in.end()) {
			key.outTangent = Engine::Vector2::FromJson(*it);
		}
		return key;
	}

	void ToJson(const Engine::AnimationTrackEditorView& view, nlohmann::json& out) {

		out = nlohmann::json::object();
		out["timeMin"] = view.timeMin;
		out["timeMax"] = view.timeMax;
		out["valueMin"] = view.valueMin;
		out["valueMax"] = view.valueMax;
	}

	Engine::AnimationTrackEditorView ParseTrackEditorView(const nlohmann::json& in) {

		Engine::AnimationTrackEditorView view{};
		if (!in.is_object()) {
			return view;
		}
		// 保存済み表示範囲が壊れていても、最低1.0の幅を確保してCurveEditorを表示できるようにする
		view.timeMin = in.value("timeMin", view.timeMin);
		view.timeMax = in.value("timeMax", view.timeMax);
		view.valueMin = in.value("valueMin", view.valueMin);
		view.valueMax = in.value("valueMax", view.valueMax);
		if (view.timeMax <= view.timeMin) {
			view.timeMax = view.timeMin + 1.0f;
		}
		if (view.valueMax <= view.valueMin) {
			view.valueMax = view.valueMin + 1.0f;
		}
		return view;
	}

	void ToJson(const Engine::AnimationLoopBridgeSettings& bridge, nlohmann::json& out) {

		out = nlohmann::json::object();
		out["enabled"] = bridge.enabled;
		out["duration"] = bridge.duration;
		out["interpolation"] = Engine::ToString(bridge.interpolation);
	}

	Engine::AnimationLoopBridgeSettings ParseLoopBridge(const nlohmann::json& in) {

		Engine::AnimationLoopBridgeSettings bridge{};
		if (!in.is_object()) {
			return bridge;
		}
		bridge.enabled = in.value("enabled", bridge.enabled);
		bridge.duration = (std::max)(in.value("duration", bridge.duration), 0.001f);
		Engine::CurveInterpolationMode interpolation = bridge.interpolation;
		if (Engine::TryParseCurveInterpolationMode(in.value("interpolation", "Linear"), interpolation)) {
			// Bridge区間ではBezierハンドルを持てないため、読み込み時はLinearへ倒す
			bridge.interpolation = interpolation == Engine::CurveInterpolationMode::Bezier ?
				Engine::CurveInterpolationMode::Linear : interpolation;
		}
		return bridge;
	}

	void ToJson(const Engine::CurveChannel& channel, nlohmann::json& out) {

		// チャンネルはキーが空でも保存して追加直後のTrack構造を維持する
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

		// キーは読み込み直後に時間順へ揃える
		if (const auto it = in.find("keys"); it != in.end() && it->is_array()) {
			for (const nlohmann::json& keyJson : *it) {
				channel.keys.emplace_back(ParseCurveKey(keyJson));
			}
			channel.SortKeys();
		}
		return channel;
	}

	void ToJson(const Engine::CurveQuaternionAxisKey& axisKey, nlohmann::json& out) {

		out = nlohmann::json::object();
		out["useCustomAxis"] = axisKey.useCustomAxis;
		out["customAxis"] = axisKey.customAxis.ToJson();
		out["axes"] = nlohmann::json::array();
		for (Engine::Axis axis : axisKey.axes) {
			out["axes"].push_back(Engine::EnumAdapter<Engine::Axis>::ToString(axis));
		}
	}

	Engine::CurveQuaternionAxisKey ParseQuaternionAxisKey(const nlohmann::json& in) {

		Engine::CurveQuaternionAxisKey axisKey = Engine::QuaternionAxisKeyUtility::MakeDefault();
		if (!in.is_object()) {
			return axisKey;
		}

		axisKey.useCustomAxis = in.value("useCustomAxis", axisKey.useCustomAxis);
		if (const auto it = in.find("customAxis"); it != in.end()) {
			axisKey.customAxis = Engine::Vector3::FromJson(*it);
		}
		axisKey.axes.clear();
		if (const auto it = in.find("axes"); it != in.end() && it->is_array()) {
			for (const nlohmann::json& axisJson : *it) {
				if (!axisJson.is_string()) {
					continue;
				}
				if (auto axisOpt = Engine::EnumAdapter<Engine::Axis>::FromString(axisJson.get<std::string>())) {
					axisKey.axes.push_back(*axisOpt);
				}
			}
		}
		if (axisKey.axes.empty()) {
			axisKey.axes.emplace_back(Engine::Axis::X);
		}
		return axisKey;
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

bool Engine::LoadAnimationClipAsset(const std::filesystem::path& path, AnimationClipAsset& outClip) {

	// AnimationClipはjson単体で管理する
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
		// ProjectPanelから作られた直後でも保存できるよう、親フォルダを先に作る
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
	out["quaternionMultiplyOrder"] = ToString(track.quaternionMultiplyOrder);
	out["visible"] = track.visible;
	ToJson(track.editorView, out["editorView"]);
	out["channels"] = nlohmann::json::array();

	for (const CurveChannel& channel : track.channels) {
		nlohmann::json channelJson;
		ToJson(channel, channelJson);
		out["channels"].push_back(channelJson);
	}

	if (track.binding.valueType == AnimationValueType::Quaternion && !track.quaternionAxisKeys.empty()) {
		out["quaternionAxisKeys"] = nlohmann::json::array();
		for (const CurveQuaternionAxisKey& axisKey : track.quaternionAxisKeys) {
			nlohmann::json axisJson;
			ToJson(axisKey, axisJson);
			out["quaternionAxisKeys"].push_back(axisJson);
		}
	}
}

void Engine::from_json(const nlohmann::json& in, AnimationCurveTrack& track) {

	from_json(in, track.binding);

	AnimationApplyMode applyMode = track.applyMode;
	if (TryParseAnimationApplyMode(in.value("applyMode", "Override"), applyMode)) {
		track.applyMode = applyMode;
	}
	QuaternionMultiplyOrder order = track.quaternionMultiplyOrder;
	if (TryParseQuaternionMultiplyOrder(in.value("quaternionMultiplyOrder", "BaseThenCurve"), order)) {
		track.quaternionMultiplyOrder = order;
	}
	track.visible = in.value("visible", track.visible);
	if (const auto it = in.find("editorView"); it != in.end()) {
		track.editorView = ParseTrackEditorView(*it);
	}

	track.channels.clear();
	if (const auto it = in.find("channels"); it != in.end() && it->is_array()) {
		for (const nlohmann::json& channelJson : *it) {
			track.channels.emplace_back(ParseCurveChannel(channelJson));
		}
	}
	track.quaternionAxisKeys.clear();
	if (const auto it = in.find("quaternionAxisKeys"); it != in.end() && it->is_array()) {
		for (const nlohmann::json& axisJson : *it) {
			track.quaternionAxisKeys.emplace_back(ParseQuaternionAxisKey(axisJson));
		}
	}
	// 手編集で不足したチャンネルを補う
	NormalizeAnimationTrackChannels(track);
}

void Engine::to_json(nlohmann::json& out, const AnimationClipAsset& clip) {

	out = nlohmann::json::object();
	out["name"] = clip.name;
	out["duration"] = clip.duration;
	out["autoDuration"] = clip.autoDuration;
	out["loop"] = clip.loop;
	out["relativeTransform"] = clip.relativeTransform;
	ToJson(clip.loopBridge, out["loopBridge"]);
	out["curveTracks"] = clip.curveTracks;
	out["events"] = nlohmann::json::array();
	for (const AnimationEvent& event : clip.events) {

		nlohmann::json eventJson;
		eventJson["time"] = event.time;
		eventJson["name"] = event.name;
		eventJson["floatParam"] = event.floatParam;
		eventJson["intParam"] = event.intParam;
		eventJson["stringParam"] = event.stringParam;
		out["events"].push_back(std::move(eventJson));
	}
}

void Engine::from_json(const nlohmann::json& in, AnimationClipAsset& clip) {

	clip.name = in.value("name", clip.name);
	clip.duration = in.value("duration", clip.duration);
	if (clip.duration <= 0.0f) {
		clip.duration = 1.0f;
	}
	clip.autoDuration = in.value("autoDuration", clip.autoDuration);
	clip.loop = in.value("loop", clip.loop);
	clip.relativeTransform = in.value("relativeTransform", clip.relativeTransform);
	if (const auto it = in.find("loopBridge"); it != in.end()) {
		clip.loopBridge = ParseLoopBridge(*it);
	}

	clip.curveTracks.clear();
	if (const auto it = in.find("curveTracks"); it != in.end() && it->is_array()) {
		for (const nlohmann::json& trackJson : *it) {
			clip.curveTracks.emplace_back(trackJson.get<AnimationCurveTrack>());
		}
	}

	clip.events.clear();
	if (const auto it = in.find("events"); it != in.end() && it->is_array()) {
		for (const nlohmann::json& eventJson : *it) {

			AnimationEvent event{};
			event.time = eventJson.value("time", 0.0f);
			event.name = eventJson.value("name", std::string());
			event.floatParam = eventJson.value("floatParam", 0.0f);
			event.intParam = eventJson.value("intParam", 0);
			event.stringParam = eventJson.value("stringParam", std::string());
			clip.events.emplace_back(std::move(event));
		}
	}
	UpdateAnimationClipAutoDuration(clip);
}

void Engine::to_json(nlohmann::json& out, const CurveChannel& channel) {

	ToJson(channel, out);
}

void Engine::from_json(const nlohmann::json& in, CurveChannel& channel) {

	channel = ParseCurveChannel(in);
}
