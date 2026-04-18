#include "IRenderItemExtractor.h"

//============================================================================
//	IRenderItemExtractor classMethods
//============================================================================

bool Engine::RenderItemExtract::IsVisible(ECSWorld& world, const Entity& entity, bool visible) {

	if (!visible || !world.IsAlive(entity)) {
		return false;
	}

	const SceneObjectComponent* sceneObject = GetSceneObject(world, entity);
	if (!sceneObject) {
		return true;
	}
	return sceneObject->activeInHierarchy;
}

Engine::Matrix4x4 Engine::RenderItemExtract::GetWorldMatrix(ECSWorld& world, const Entity& entity) {

	if (world.HasComponent<Engine::TransformComponent>(entity)) {

		return world.GetComponent<Engine::TransformComponent>(entity).worldMatrix;
	}
	return Engine::Matrix4x4::Identity();
}

const Engine::SceneObjectComponent* Engine::RenderItemExtract::GetSceneObject(ECSWorld& world, const Entity& entity) {

	if (!world.IsAlive(entity)) {
		return nullptr;
	}
	if (!world.HasComponent<SceneObjectComponent>(entity)) {
		return nullptr;
	}
	return &world.GetComponent<SceneObjectComponent>(entity);
}