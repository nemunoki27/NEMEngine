#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/MathLib/Math.h>

namespace Engine {

	//============================================================================
	//	SpotLightComponent struct
	//============================================================================

	// スポット光源
	struct SpotLightComponent {

		// 色
		Color4 color = Color4::White();
		// 方向
		Vector3 direction = Vector3(0.0f, 0.0f, 1.0f);

		// 強さ
		float intensity = 1.0f;
		// 距離
		float distance = 10.0f;
		// 減衰
		float decay = 1.0f;
		// 影響角度
		float cosAngle = std::cos(Math::pi / 3.0f);
		float cosFalloffStart = std::cos(Math::pi / 6.0f);

		// 有効フラグ
		bool enabled = true;
		// 
		uint32_t affectLayerMask = 0xffffffffu;
	};

	// json変換
	void from_json(const nlohmann::json& in, SpotLightComponent& component);
	void to_json(nlohmann::json& out, const SpotLightComponent& component);

	ENGINE_REGISTER_COMPONENT(SpotLightComponent, "SpotLight");
} // Engine