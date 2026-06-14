#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Color.h>

namespace Engine {

	//============================================================================
	//	SkyboxRendererComponent struct
	//	シーンの背景cubemapを描画する
	//============================================================================
	struct SkyboxRendererComponent {

		// 背景に使うcubemapテクスチャ
		AssetID cubemapTexture{};
		// cubemapへ掛ける色
		Color4 color = Color4::White();
		// 表示フラグ
		bool visible = true;
	};

	// json変換
	void from_json(const nlohmann::json& in, SkyboxRendererComponent& component);
	void to_json(nlohmann::json& out, const SkyboxRendererComponent& component);

	ENGINE_REGISTER_COMPONENT(SkyboxRendererComponent, "SkyboxRenderer");
} // Engine
