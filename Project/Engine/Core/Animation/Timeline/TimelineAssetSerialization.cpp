#include "TimelineAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace Engine;

namespace {

	constexpr std::array<std::string_view, 15> kTrackNames = {
		"Group", "SkinnedAnimation", "Motion", "Particle", "Prefab", "Collision", "Camera", "Audio",
		"Property", "Activation", "TimeScale", "Event", "Window", "SubTimeline", "Custom"
	};

	// 未設定の参照だけを無効値として扱う
	UUID ReadID(const nlohmann::json& in, const char* key) {

		const std::string value = in.value(key, std::string{});
		if (value.empty()) return {};
		const auto id = TryParseUUID16Hex(value);
		if (!id) throw std::invalid_argument(std::string(key) + "の識別子が不正です");
		return *id;
	}

	// 保存用の参照表記を統一する
	std::string WriteID(UUID id) {

		return id ? ToString(id) : std::string{};
	}

	// 条件の設定値を読み取る
	TimelineCondition ReadCondition(const nlohmann::json& in) {

		TimelineCondition condition;
		condition.kind = static_cast<TimelineConditionKind>(in.value("kind", 0));
		condition.comparison = static_cast<TimelineComparison>(in.value("comparison", 0));
		condition.plane = static_cast<TimelineDistancePlane>(in.value("plane", 0));
		condition.parameter = ReadID(in, "parameter");
		condition.firstBinding = ReadID(in, "firstBinding");
		condition.secondBinding = ReadID(in, "secondBinding");
		condition.distance = in.value("distance", 1.0f);
		if (in.contains("value")) Engine::from_json(in.at("value"), condition.value);
		return condition;
	}

	// クリップの保存値を読み取る
	TimelineClip ReadClip(const nlohmann::json& in) {

		TimelineClip clip;
		clip.id = ReadID(in, "id");
		clip.name = in.value("name", std::string{});
		clip.start = in.value("start", 0.0);
		clip.duration = in.value("duration", 1.0);
		clip.sourceDuration = in.value("sourceDuration", 0.0);
		clip.clipIn = in.value("clipIn", 0.0);
		clip.speed = in.value("speed", 1.0);
		clip.blendIn = in.value("blendIn", 0.0);
		clip.blendOut = in.value("blendOut", 0.0);
		clip.enabled = in.value("enabled", true);
		const auto easing = EnumAdapter<EasingType>::FromString(in.value("easing", std::string{"Linear"}));
		if (!easing) throw std::invalid_argument("イージングの種類が不正です");
		clip.easing = *easing;
		clip.data = in.value("data", nlohmann::json::object());
		return clip;
	}
}

std::string_view Engine::GetTimelineTrackKindName(TimelineTrackKind kind) {

	const auto index = static_cast<size_t>(kind);
	return index < kTrackNames.size() ? kTrackNames[index] : std::string_view{};
}

bool Engine::ParseTimelineTrackKind(std::string_view name, TimelineTrackKind& kind) {

	for (size_t i = 0; i < kTrackNames.size(); ++i) {
		if (name != kTrackNames[i]) continue;
		kind = static_cast<TimelineTrackKind>(i);
		return true;
	}
	return false;
}

void Engine::to_json(nlohmann::json& out, const TimelineParameterValue& value) {

	constexpr std::array<const char*, 5> names = {"Bool", "Int", "Float", "Vector3", "String"};
	out = {{"type", names[value.index()]}};
	std::visit([&](const auto& typed) {
		using T = std::decay_t<decltype(typed)>;
		if constexpr (std::is_same_v<T, Vector3>) out["value"] = typed.ToJson();
		else out["value"] = typed;
	}, value);
}

void Engine::from_json(const nlohmann::json& in, TimelineParameterValue& value) {

	const auto& data = in.is_object() ? in.at("value") : in;
	const std::string type = in.is_object() ? in.value("type", "Float") :
		(data.is_boolean() ? "Bool" : data.is_string() ? "String" : data.is_number_integer() ? "Int" : "Float");
	if (type == "Bool") value = data.get<bool>();
	else if (type == "Int") {
		const auto number = data.get<int64_t>();
		if (number < (std::numeric_limits<int32_t>::min)() || number > (std::numeric_limits<int32_t>::max)())
			throw std::invalid_argument("整数パラメーターが範囲外です");
		value = static_cast<int32_t>(number);
	} else if (type == "Float") {
		const float number = data.get<float>();
		if (!std::isfinite(number)) throw std::invalid_argument("数値パラメーターが不正です");
		value = number;
	} else if (type == "Vector3") {
		const auto vector = Vector3::FromJson(data);
		if (!std::isfinite(vector.x) || !std::isfinite(vector.y) || !std::isfinite(vector.z))
			throw std::invalid_argument("ベクトルパラメーターが不正です");
		value = vector;
	} else if (type == "String") value = data.get<std::string>();
	else throw std::invalid_argument("パラメーターの型が不正です");
}

void Engine::to_json(nlohmann::json& out, const TimelineAsset& asset) {

	out = {{"schemaVersion", asset.schemaVersion}, {"name", asset.name}, {"duration", GetTimelineAssetDuration(asset)},
		{"frameRate", asset.frameRate}, {"durationMode", asset.durationMode == TimelineDurationMode::BasedOnClips ? "BasedOnClips" : "FixedLength"},
		{"bindings", nlohmann::json::array()},
		{"parameters", nlohmann::json::array()}, {"tracks", nlohmann::json::array()}};
	for (const auto& binding : asset.bindings) out["bindings"].push_back({
		{"id", WriteID(binding.id)}, {"name", binding.name}, {"component", binding.component}});
	for (const auto& parameter : asset.parameters) {
		nlohmann::json value;
		Engine::to_json(value, parameter.value);
		out["parameters"].push_back({{"id", WriteID(parameter.id)}, {"name", parameter.name}, {"value", value}});
	}
	for (const auto& track : asset.tracks) {
		nlohmann::json value = {{"id", WriteID(track.id)}, {"name", track.name},
			{"kind", GetTimelineTrackKindName(track.kind)}, {"binding", WriteID(track.binding)},
			{"parent", WriteID(track.parent)}, {"branchGroup", WriteID(track.branchGroup)},
			{"muted", track.muted}, {"locked", track.locked}, {"fallback", track.fallback},
			{"conditionsAll", track.conditionsAll}, {"start", track.start}, {"duration", track.duration},
			{"conditions", nlohmann::json::array()}, {"clips", nlohmann::json::array()}};
		for (const auto& condition : track.conditions) {
			nlohmann::json parameter;
			Engine::to_json(parameter, condition.value);
			value["conditions"].push_back({{"kind", static_cast<int>(condition.kind)},
				{"comparison", static_cast<int>(condition.comparison)}, {"parameter", WriteID(condition.parameter)},
				{"firstBinding", WriteID(condition.firstBinding)}, {"secondBinding", WriteID(condition.secondBinding)},
				{"value", parameter}, {"plane", static_cast<int>(condition.plane)}, {"distance", condition.distance}});
		}
		for (const auto& clip : track.clips) value["clips"].push_back({{"id", WriteID(clip.id)},
			{"name", clip.name}, {"start", clip.start}, {"duration", clip.duration}, {"clipIn", clip.clipIn},
			{"sourceDuration", clip.sourceDuration},
			{"speed", clip.speed}, {"blendIn", clip.blendIn}, {"blendOut", clip.blendOut},
			{"enabled", clip.enabled}, {"easing", EnumAdapter<EasingType>::ToString(clip.easing)}, {"data", clip.data}});
		out["tracks"].push_back(std::move(value));
	}
}

void Engine::from_json(const nlohmann::json& in, TimelineAsset& asset) {

	TimelineAsset candidate;
	candidate.schemaVersion = in.value("schemaVersion", 1u);
	candidate.name = in.value("name", std::string{});
	candidate.duration = in.value("duration", 1.0);
	candidate.frameRate = in.value("frameRate", 60.0);
	const std::string durationMode = in.value("durationMode", std::string{"FixedLength"});
	if (durationMode != "FixedLength" && durationMode != "BasedOnClips")
		throw std::invalid_argument("再生時間の設定が不正です");
	candidate.durationMode = durationMode == "BasedOnClips" ? TimelineDurationMode::BasedOnClips : TimelineDurationMode::FixedLength;
	for (const auto& value : in.value("bindings", nlohmann::json::array())) candidate.bindings.push_back({
		ReadID(value, "id"), value.value("name", std::string{}), value.value("component", std::string{})});
	for (const auto& value : in.value("parameters", nlohmann::json::array())) {
		TimelineParameterDefinition parameter{ReadID(value, "id"), value.value("name", std::string{})};
		if (value.contains("value")) Engine::from_json(value.at("value"), parameter.value);
		candidate.parameters.push_back(std::move(parameter));
	}
	for (const auto& value : in.value("tracks", nlohmann::json::array())) {
		TimelineTrack track;
		track.id = ReadID(value, "id");
		track.name = value.value("name", std::string{});
		if (!ParseTimelineTrackKind(value.value("kind", std::string{"Property"}), track.kind))
			throw std::invalid_argument("トラックの種類が不正です");
		track.binding = ReadID(value, "binding");
		track.parent = ReadID(value, "parent");
		track.branchGroup = ReadID(value, "branchGroup");
		track.muted = value.value("muted", false);
		track.locked = value.value("locked", false);
		track.fallback = value.value("fallback", false);
		track.conditionsAll = value.value("conditionsAll", true);
		track.start = value.value("start", 0.0);
		track.duration = value.value("duration", 0.0);
		for (const auto& condition : value.value("conditions", nlohmann::json::array()))
			track.conditions.push_back(ReadCondition(condition));
		for (const auto& clip : value.value("clips", nlohmann::json::array())) track.clips.push_back(ReadClip(clip));
		candidate.tracks.push_back(std::move(track));
	}
	UpdateTimelineAssetDuration(candidate);
	std::string diagnostic;
	if (!ValidateTimelineAsset(candidate, diagnostic)) throw std::invalid_argument(diagnostic);
	asset = std::move(candidate);
}
