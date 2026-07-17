#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	UIButtonComponent struct
	//	UISelectableの決定入力をクリックとして公開する
	//============================================================================
	struct UIButtonComponent {

		bool enabled = true;
		std::string actionName{};

		bool runtimeClickedThisFrame = false;
	};

	void from_json(const nlohmann::json& in, UIButtonComponent& component);
	void to_json(nlohmann::json& out, const UIButtonComponent& component);

	ENGINE_REGISTER_COMPONENT(UIButtonComponent, "UIButton");
} // Engine
