#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Math/Color.h>

namespace Engine {

	//============================================================================
	//	PointLightComponent struct
	//============================================================================
	// 点光源
	struct PointLightComponent {

		// 色
		Color4 color = Color4::White();

		// 強さ
		float intensity = 1.0f;
		// 半径
		float radius = 8.0f;
		// 減衰
		float decay = 1.0f;
		// 影の強さ(0.0=影なし, 1.0=完全に黒)
		float shadowStrength = 0.92f;

		// 有効フラグ
		bool enabled = true;
		// 
		uint32_t affectLayerMask = 0xffffffffu;
	};

	// json変換
	void from_json(const nlohmann::json& in, PointLightComponent& component);
	void to_json(nlohmann::json& out, const PointLightComponent& component);

} // Engine