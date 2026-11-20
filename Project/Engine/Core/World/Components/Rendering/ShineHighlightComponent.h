#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Color.h>

namespace Engine {

	//============================================================================
	//	ShineHighlightComponent struct
	//	光の帯の設定
	//============================================================================
	struct ShineHighlightComponent {

		static constexpr ComponentChangeChannel kChangeChannels = ComponentChangeChannel::Render;
		bool enabled = true;
		float position = 0.0f;
		Color4 color = Color4::White();
		float intensity = 1.0f;
		float angle = 25.0f;
		float width = 0.18f;
		float softness = 0.5f;
		AssetID rampTexture{};
	};

	void from_json(const nlohmann::json& in, ShineHighlightComponent& component);
	void to_json(nlohmann::json& out, const ShineHighlightComponent& component);

} // Engine
