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

namespace {

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

	void ToJson(const Engine::AnimationLoopBridgeSettings& bridge, nlohmann::json& out) {

		out = nlohmann::json::object();
		out["enabled"] = bridge.enabled;
		out["duration"] = bridge.duration;
		out["interpolation"] = Engine::ToString(bridge.interpolation);
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
