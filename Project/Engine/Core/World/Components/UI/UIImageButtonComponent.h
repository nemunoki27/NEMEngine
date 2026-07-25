#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	UIImageButtonComponent struct
	//	画像ボタンのクリック状態を公開する
	//============================================================================
	struct UIImageButtonComponent {

		bool enabled = true;
		std::string actionName{};

		// フレーム中にクリックされたか
		bool runtimeClickedThisFrame = false;
	};

	void from_json(const nlohmann::json& in, UIImageButtonComponent& component);
	void to_json(nlohmann::json& out, const UIImageButtonComponent& component);

} // Engine
