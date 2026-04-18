#include "SceneAuthoring.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>

//============================================================================
//	SceneAuthoring classMethods
//============================================================================

Engine::Entity Engine::SceneAuthoring::CreateGameObject(ECSWorld& world, const std::string_view& name) {

	// エンティティの作成
	Entity entity = world.CreateEntity();
	// デフォルトコンポーネントの追加
	EnsureGameObjectDefaults(world, entity, name);
	return entity;
}

void Engine::SceneAuthoring::EnsureGameObjectDefaults(ECSWorld& world,
	const Entity& entity, const std::string_view& defaultName) {

	// エンティティが存在しない場合は何もしない
	if (!world.IsAlive(entity)) {
		return;
	}

	// デフォルトコンポーネントの追加
	if (!world.HasComponent<NameComponent>(entity)) {

		auto& name = world.AddComponent<NameComponent>(entity);
		name.name = std::string(defaultName);
	}
	if (!world.HasComponent<TransformComponent>(entity)) {

		world.AddComponent<TransformComponent>(entity);
	}
	if (!world.HasComponent<HierarchyComponent>(entity)) {

		world.AddComponent<HierarchyComponent>(entity);
	}
	if (!world.HasComponent<SceneObjectComponent>(entity)) {

		auto& sceneObject = world.AddComponent<SceneObjectComponent>(entity);
		sceneObject.localFileID = UUID::New();
	}

	auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
	if (!sceneObject.localFileID) {

		sceneObject.localFileID = UUID::New();
	}
}