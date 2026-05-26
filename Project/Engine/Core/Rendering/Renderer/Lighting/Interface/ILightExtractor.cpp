#include "ILightExtractor.h"

//============================================================================
//	ILightExtractor classMethods
//============================================================================

bool Engine::LightExtract::IsVisible(ECSWorld& world, const Entity& entity, bool enabled) {

	if (!enabled || !world.IsAlive(entity)) {
		return false;
	}

	const SceneObjectComponent* sceneObject = GetSceneObject(world, entity);
	if (!sceneObject) {
		return true;
	}
	return sceneObject->activeInHierarchy;
}

Engine::Matrix4x4 Engine::LightExtract::GetWorldMatrix(ECSWorld& world, const Entity& entity) {

	if (const auto* transform = world.TryGetComponent<TransformComponent>(entity)) {
		return transform->worldMatrix;
	}
	return Matrix4x4::Identity();
}

const Engine::SceneObjectComponent* Engine::LightExtract::GetSceneObject(ECSWorld& world, const Entity& entity) {

	return world.TryGetComponent<SceneObjectComponent>(entity);
}

Engine::Vector3 Engine::LightExtract::GetWorldPos(ECSWorld& world, const Entity& entity) {

	return GetWorldMatrix(world, entity).GetTranslationValue();
}

Engine::Vector3 Engine::LightExtract::GetWorldDirection(
	const Vector3& localDirection, const Matrix4x4& worldMatrix) {

	return Vector3::TransferNormal(localDirection.Normalize(), worldMatrix).Normalize();
}
