#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Math/Color.h>

namespace Engine {

	//============================================================================
	//	RectLightComponent struct
	//============================================================================
	// 矩形面光源
	struct RectLightComponent {

		static constexpr ComponentChangeChannel kChangeChannels =
			ComponentChangeChannel::Lighting;
		static constexpr ComponentChangeChannel kTransformChannels =
			ComponentChangeChannel::Lighting;

		// 色
		Color4 color = Color4::White();

		// 強さ
		float intensity = 1.0f;
		// 減衰半径
		float attenuationRadius = 10.0f;
		// 光源幅
		float sourceWidth = 2.0f;
		// 光源高さ
		float sourceHeight = 2.0f;
		// 減衰
		float decay = 1.0f;
		// バーンドア角度
		float barnDoorAngle = 88.0f;
		// バーンドア長さ
		float barnDoorLength = 0.0f;
		// 影の強さ
		float shadowStrength = 0.92f;

		// 有効フラグ
		bool enabled = true;
		// 影響レイヤー
		uint32_t affectLayerMask = 0xffffffffu;
	};

	// json変換
	void from_json(const nlohmann::json& in, RectLightComponent& component);
	void to_json(nlohmann::json& out, const RectLightComponent& component);

} // Engine
