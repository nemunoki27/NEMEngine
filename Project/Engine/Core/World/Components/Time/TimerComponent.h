#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Time/LocalTime.h>

// c++
#include <cstdint>
#include <string>

namespace Engine {

	enum class TimerMode { Seconds, Frames };

	// 秒数と基準フレーム数で進行するタイマー
	struct TimerComponent {

		bool enabled = true;
		bool playOnStart = false;
		bool playInEditMode = false;
		TimerMode mode = TimerMode::Seconds;
		float targetSeconds = 1.0f;
		int32_t targetFrames = 60;
		float baseFPS = 60.0f;
		TimeSourceSettings timeSource{};
		double runtimeElapsed = 0.0;
		float runtimeSeconds = 0.0f;
		int32_t runtimeFrames = 0;
		int32_t runtimeRequest = 0;
		bool runtimeInitialized = false;
		bool runtimePlaying = false;
		bool runtimePaused = false;
		bool runtimeReached = false;
		std::string runtimeDiagnostic;
	};

	void from_json(const nlohmann::json& in, TimerComponent& component);
	void to_json(nlohmann::json& out, const TimerComponent& component);
	void DeserializeComponent(ECSWorld& world, const Entity& entity, const nlohmann::json& in, TimerComponent& component);
	void ApplyTimerSettings(const TimerComponent& source, TimerComponent& target);
	void UpdateTimer(TimerComponent& component, double deltaTime);
}
