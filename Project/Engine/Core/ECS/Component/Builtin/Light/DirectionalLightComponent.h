#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/MathLib/Vector3.h>
#include <Engine/MathLib/Color.h>

namespace Engine {

	//============================================================================
	//	DirectionalLightComponent struct
	//============================================================================

	// 平行光源
	struct DirectionalLightComponent {

		// 色
		Color color = Color::White();
		// 方向
		Vector3 direction = Vector3(0.0f, -1.0f, 0.0f);

		// 強さ
		float intensity = 1.0f;

		// 有効フラグ
		bool enabled = true;
		// 
		uint32_t affectLayerMask = 0xffffffffu;
	};

	// json変換
	void from_json(const nlohmann::json& in, DirectionalLightComponent& component);
	void to_json(nlohmann::json& out, const DirectionalLightComponent& component);

	ENGINE_REGISTER_COMPONENT(DirectionalLightComponent, "DirectionalLight");
} // Engine