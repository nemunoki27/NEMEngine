#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>

namespace Engine {

	//============================================================================
	//	NameComponent struct
	//============================================================================

	// 名前
	struct NameComponent {

		std::string name = "Entity";
	};

	// json変換
	void from_json(const nlohmann::json& in, NameComponent& component);
	void to_json(nlohmann::json& out, const NameComponent& component);

	ENGINE_REGISTER_COMPONENT(NameComponent, "Name");
} // Engine