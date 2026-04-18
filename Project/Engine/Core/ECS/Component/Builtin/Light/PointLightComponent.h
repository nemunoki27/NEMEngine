#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/MathLib/Color.h>

namespace Engine {

	//============================================================================
	//	PointLightComponent struct
	//============================================================================

	// 点光源
	struct PointLightComponent {

		// 色
		Color color = Color::White();

		// 強さ
		float intensity = 1.0f;
		// 半径
		float radius = 8.0f;
		// 減衰
		float decay = 1.0f;

		// 有効フラグ
		bool enabled = true;
		// 
		uint32_t affectLayerMask = 0xffffffffu;
	};

	// json変換
	void from_json(const nlohmann::json& in, PointLightComponent& component);
	void to_json(nlohmann::json& out, const PointLightComponent& component);

	ENGINE_REGISTER_COMPONENT(PointLightComponent, "PointLight");
} // Engine