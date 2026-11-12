#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

// c++
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace Engine {

	enum class TimelineTrackKind : uint8_t {

		Group, SkinnedAnimation, Motion, Particle, Prefab, Collision, Camera, Audio,
		Property, Activation, TimeScale, Event, Window, SubTimeline, Custom
	};
	enum class TimelineConditionKind : uint8_t { Parameter, Distance, BindingExists };
	enum class TimelineComparison : uint8_t { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
	enum class TimelineDistancePlane : uint8_t { Full3D, GroundXZ, PlaneXY };
	enum class TimelineDurationMode : uint8_t { FixedLength, BasedOnClips };
	using TimelineParameterValue = std::variant<bool, int32_t, float, Vector3, std::string>;

	struct TimelineBindingDefinition {

		UUID id{};
		std::string name;
		std::string component;
	};
	struct TimelineParameterDefinition {

		UUID id{};
		std::string name;
		TimelineParameterValue value = 0.0f;
	};
	struct TimelineCondition {

		TimelineConditionKind kind = TimelineConditionKind::Parameter;
		TimelineComparison comparison = TimelineComparison::Equal;
		UUID parameter{}, firstBinding{}, secondBinding{};
		TimelineParameterValue value = true;
		TimelineDistancePlane plane = TimelineDistancePlane::Full3D;
		float distance = 1.0f;
	};
	struct TimelineClip {

		UUID id{};
		std::string name;
		double start = 0.0;
		double duration = 1.0;
		double sourceDuration = 0.0;
		double clipIn = 0.0;
		double speed = 1.0;
		double blendIn = 0.0;
		double blendOut = 0.0;
		bool enabled = true;
		EasingType easing = EasingType::Linear;
		nlohmann::json data = nlohmann::json::object();
	};
	struct TimelineTrack {

		UUID id{};
		std::string name;
		TimelineTrackKind kind = TimelineTrackKind::Property;
		UUID binding{}, parent{}, branchGroup{};
		bool muted = false;
		bool locked = false;
		bool fallback = false;
		bool conditionsAll = true;
		double start = 0.0;
		double duration = 0.0;
		std::vector<TimelineCondition> conditions;
		std::vector<TimelineClip> clips;
	};
	struct TimelineAsset {

		AssetID guid{};
		uint32_t schemaVersion = 1;
		std::string name;
		double duration = 1.0;
		double frameRate = 60.0;
		TimelineDurationMode durationMode = TimelineDurationMode::FixedLength;
		std::vector<TimelineBindingDefinition> bindings;
		std::vector<TimelineParameterDefinition> parameters;
		std::vector<TimelineTrack> tracks;
	};

	// 保存名からトラックの種類を解決する
	std::string_view GetTimelineTrackKindName(TimelineTrackKind kind);
	bool ParseTimelineTrackKind(std::string_view name, TimelineTrackKind& kind);
	// 新規作成時の識別子と初期設定を用意する
	TimelineAsset CreateDefaultTimelineAsset(std::string name);
	// 配置した長さと素材の評価時間を分ける
	double GetTimelineClipSourceDuration(const TimelineClip& clip);
	double GetTimelineAssetDuration(const TimelineAsset& asset);
	// 自動長のアセットだけクリップ末尾へ合わせる
	void UpdateTimelineAssetDuration(TimelineAsset& asset);
	// 編集データと参照関係を検証する
	bool ValidateTimelineAsset(const TimelineAsset& asset, std::string& diagnostic);
	// 正常な内容だけ読み込み先へ反映する
	bool LoadTimelineAsset(const std::filesystem::path& path, TimelineAsset& asset, std::string* diagnostic = nullptr);
	bool SaveTimelineAsset(const std::filesystem::path& path, const TimelineAsset& asset, std::string* diagnostic = nullptr);
	void from_json(const nlohmann::json& in, TimelineAsset& asset);
	void to_json(nlohmann::json& out, const TimelineAsset& asset);
	void from_json(const nlohmann::json& in, TimelineParameterValue& value);
	void to_json(nlohmann::json& out, const TimelineParameterValue& value);
}
