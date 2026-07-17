#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	UITextButtonComponent struct
	//	テキストボタンのクリック状態を公開する
	//============================================================================
	struct UITextButtonComponent {

		bool enabled = true;
		std::string actionName{};

		// フレーム中にクリックされたか
		bool runtimeClickedThisFrame = false;
	};

	void from_json(const nlohmann::json& in, UITextButtonComponent& component);
	void to_json(nlohmann::json& out, const UITextButtonComponent& component);

	ENGINE_REGISTER_COMPONENT(UITextButtonComponent, "UITextButton");
} // Engine
