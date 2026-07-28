#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Color.h>

namespace Engine {

	//============================================================================
	//	DirectionalLightComponent struct
	//============================================================================
	// 平行光源
	struct DirectionalLightComponent {

		static constexpr ComponentChangeChannel kChangeChannels =
			ComponentChangeChannel::Lighting;
		static constexpr ComponentChangeChannel kTransformChannels =
			ComponentChangeChannel::Lighting;

		// 色
		Color4 color = Color4::White();
		// 方向
		Vector3 direction = Vector3(0.0f, -1.0f, 0.0f);

		// 強さ
		float intensity = 10.0f;
		// 影の強さ(0.0=影なし, 1.0=完全に黒)
		float shadowStrength = 0.92f;

		// 有効フラグ
		bool enabled = true;
		uint32_t affectLayerMask = 0xffffffffu;
	};

	// json変換
	void from_json(const nlohmann::json& in, DirectionalLightComponent& component);
	void to_json(nlohmann::json& out, const DirectionalLightComponent& component);

} // Engine
