#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

namespace Engine {

	//============================================================================
	//	TimeScaleComponent struct
	//============================================================================
	
	// プレイ中のデルタタイムスケール
	struct TimeScaleComponent {

		// 1.0fで等速、0.0fで停止
		float timeScale = 1.0f;
	};

	// json変換
	void from_json(const nlohmann::json& in, TimeScaleComponent& component);
	void to_json(nlohmann::json& out, const TimeScaleComponent& component);

	ENGINE_REGISTER_COMPONENT(TimeScaleComponent, "TimeScale");
} // Engine
