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
	struct UIImageButtonRuntimeComponent {

		static constexpr bool kSerializable = false;

		bool clickedThisFrame = false;
	};

	struct UIImageButtonComponent {

		static constexpr bool kHasECSHooks = true;

		bool enabled = true;
		std::string actionName{};

		static void OnAdded(
			ECSWorld& world, const Entity& entity, UIImageButtonComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, UIImageButtonComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, UIImageButtonComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, UIImageButtonComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const UIImageButtonComponent& component, nlohmann::json& out);
	};

	void from_json(const nlohmann::json& in, UIImageButtonComponent& component);
	void to_json(nlohmann::json& out, const UIImageButtonComponent& component);

} // Engine
