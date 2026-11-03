#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

namespace Engine {

	//============================================================================
	//	TimeScaleComponent struct
	//============================================================================
	
	// 自身と子階層に適用するローカル時間倍率
	struct TimeScaleComponent {

		// 1.0fで等速、0.0fで停止
		float timeScale = 1.0f;
		bool enabled = true;
		bool applyToChildren = true;
		bool inheritParentScale = true;
		uint32_t channels = 127;
	};

	// json変換
	void from_json(const nlohmann::json& in, TimeScaleComponent& component);
	void to_json(nlohmann::json& out, const TimeScaleComponent& component);

} // Engine
