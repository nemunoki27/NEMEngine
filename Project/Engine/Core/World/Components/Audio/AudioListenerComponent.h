#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

namespace Engine {

	// Worldで1つだけ有効にする受音Component
	struct AudioListenerComponent {
		bool enabled = true;
	};

	void from_json(const nlohmann::json& in, AudioListenerComponent& component);
	void to_json(nlohmann::json& out, const AudioListenerComponent& component);
}
