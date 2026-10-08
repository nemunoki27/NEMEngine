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

uint64_t Engine::IRenderItemExtractor::GetContentRevision() const {

	return 0;
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

uint32_t Engine::RenderItemExtract::GetVisibilityLayerMask(
	ECSWorld& world, const Entity& entity, uint32_t renderingLayerMask) {

	// Rendererの設定を全描画経路の選別へ渡す
	const SceneObjectComponent* sceneObject = GetSceneObject(world, entity);
	const uint32_t sceneMask = sceneObject ? sceneObject->visibilityLayerMask : kRenderingLayerMaskBits;
	return sceneMask & renderingLayerMask & kRenderingLayerMaskBits;
}
