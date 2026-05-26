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

	if (const auto* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {

		return transform->worldMatrix;
	}
	return Engine::Matrix4x4::Identity();
}

const Engine::SceneObjectComponent* Engine::RenderItemExtract::GetSceneObject(ECSWorld& world, const Entity& entity) {

	return world.TryGetComponent<SceneObjectComponent>(entity);
}
