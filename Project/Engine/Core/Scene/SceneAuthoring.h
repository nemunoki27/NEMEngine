#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/World/ECSWorld.h>

//============================================================================
//	SceneAuthoring namespace
//	シーンエディタ関連のコードをまとめるための名前空間
//============================================================================
namespace Engine::SceneAuthoring {

	// 新しいゲームオブジェクトを作成する
	Entity CreateGameObject(ECSWorld& world, const std::string_view& name = "Entity");
	// ゲームオブジェクトのデフォルトコンポーネントを追加する
	void EnsureGameObjectDefaults(ECSWorld& world, const Entity& entity, const std::string_view& defaultName = "Entity");
} // Engine