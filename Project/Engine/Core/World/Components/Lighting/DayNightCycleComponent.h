#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Time/LocalTime.h>
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	DayNightCycleComponent struct
	//	一日の時刻と太陽・月の軌道を設定する
	//============================================================================
	struct DayNightCycleComponent {

		static constexpr ComponentChangeChannel kChangeChannels = ComponentChangeChannel::Lighting;
		bool enabled = true;
		bool fixedTime = false;
		bool playInEditMode = false;
		float startHour = 12.0f;
		float dayDurationSeconds = 1200.0f;
		float orbitAzimuth = 0.0f;
		float noonAltitude = 60.0f;
		float moonTimeOffset = 12.0f;
		SceneEntityReference sunEntity{};
		SceneEntityReference moonEntity{};
		SceneEntityReference atmosphereEntity{};
		TimeSourceSettings timeSource{TimeSourceMode::Global};
		double runtimeHour = 12.0;
		float runtimeCurrentHour = 12.0f;
		float runtimeSeekHour = 12.0f;
		float runtimeConfiguredStart = 12.0f;
		int32_t runtimeRequest = 0;
		bool runtimeInitialized = false;
		bool runtimePlaying = false;
		bool runtimePaused = false;
		Vector3 runtimeSunDirection{0.0f, -1.0f, 0.0f};
		Vector3 runtimeMoonDirection{0.0f, 1.0f, 0.0f};
		std::string runtimeDiagnostic;
	};

	void from_json(const nlohmann::json& in, DayNightCycleComponent& component);
	void to_json(nlohmann::json& out, const DayNightCycleComponent& component);
	void DeserializeComponent(ECSWorld& world, const Entity& entity, const nlohmann::json& in, DayNightCycleComponent& component);
	void ApplyDayNightSettings(const DayNightCycleComponent& source, DayNightCycleComponent& target);
	void UpdateDayNightClock(DayNightCycleComponent& component, double deltaTime, bool advance);
	Vector3 EvaluateDayNightDirection(double hour, float azimuth, float altitude);
}
