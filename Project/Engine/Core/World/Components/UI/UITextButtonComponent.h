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
	struct UITextButtonRuntimeComponent {

		static constexpr bool kSerializable = false;

		bool clickedThisFrame = false;
	};

	struct UITextButtonComponent {

		static constexpr bool kHasECSHooks = true;

		bool enabled = true;
		std::string actionName{};

		static void OnAdded(
			ECSWorld& world, const Entity& entity, UITextButtonComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, UITextButtonComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, UITextButtonComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, UITextButtonComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const UITextButtonComponent& component, nlohmann::json& out);
	};

	void from_json(const nlohmann::json& in, UITextButtonComponent& component);
	void to_json(nlohmann::json& out, const UITextButtonComponent& component);

} // Engine
