#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	UIImageButtonRuntimeComponent struct
	//	画像ボタンのクリック状態を公開する
	//============================================================================
	struct UIImageButtonRuntimeComponent {

		static constexpr bool kSerializable = false;

		bool clickedThisFrame = false;
	};

	//============================================================================
	//	UIImageButtonComponent struct
	//	ボタンの有効状態と操作名
	//============================================================================
	struct UIImageButtonComponent {

		static constexpr bool kHasECSHooks = true;

		bool enabled = true;
		std::string actionName{};

		// 登録時に呼ばれる実行状態の追加と解放
		static void OnAdded(ECSWorld& world, const Entity& entity, UIImageButtonComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(ECSWorld& world, const Entity& entity, UIImageButtonComponent& component);
		static void ReleaseStorage(ECSWorld& world, const Entity& entity, UIImageButtonComponent& component);
		// 設定をJSONから読み込む
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, UIImageButtonComponent& component);
		// 設定をJSONへ保存する
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const UIImageButtonComponent& component, nlohmann::json& out);
	};

	void from_json(const nlohmann::json& in, UIImageButtonComponent& component);
	void to_json(nlohmann::json& out, const UIImageButtonComponent& component);

} // Engine
