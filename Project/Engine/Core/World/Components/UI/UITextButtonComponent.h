#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	UITextButtonRuntimeComponent struct
	//	テキストボタンのクリック状態を公開する
	//============================================================================
	struct UITextButtonRuntimeComponent {

		static constexpr bool kSerializable = false;

		bool clickedThisFrame = false;
	};

	//============================================================================
	//	UITextButtonComponent struct
	//	ボタンの有効状態と操作名
	//============================================================================
	struct UITextButtonComponent {

		static constexpr bool kHasECSHooks = true;

		bool enabled = true;
		std::string actionName{};

		// 登録時に呼ばれる実行状態の追加と解放
		static void OnAdded(ECSWorld& world, const Entity& entity, UITextButtonComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(ECSWorld& world, const Entity& entity, UITextButtonComponent& component);
		static void ReleaseStorage(ECSWorld& world, const Entity& entity, UITextButtonComponent& component);
		// 設定をJSONから読み込む
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, UITextButtonComponent& component);
		// 設定をJSONへ保存する
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const UITextButtonComponent& component, nlohmann::json& out);
	};

	void from_json(const nlohmann::json& in, UITextButtonComponent& component);
	void to_json(nlohmann::json& out, const UITextButtonComponent& component);

} // Engine
