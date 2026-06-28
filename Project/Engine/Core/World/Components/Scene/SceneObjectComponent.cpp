#include "SceneObjectComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

//============================================================================
//	SceneObjectComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, SceneObjectComponent& component) {

	const std::string localID = in.value("localFileId", "");
	component.localFileID = localID.empty() ? UUID::New() : FromString16Hex(localID);
	component.activeSelf = in.value("activeSelf", true);
	component.tag = in.value("tag", std::string("Untagged"));
	component.visibilityLayerMask = in.value("visibilityLayerMask", 0xFFFFFFFFu);

	// ランタイム使用用のIDはシリアライズされないため、初期化しておく
	component.sourceAsset = AssetID{};
	component.sceneInstanceID = UUID{};
	component.activeInHierarchy = component.activeSelf;
}

void Engine::to_json(nlohmann::json& out, const SceneObjectComponent& component) {

	out["localFileId"] = component.localFileID ? ToString(component.localFileID) : "";
	out["activeSelf"] = component.activeSelf;
	out["tag"] = component.tag;
	out["visibilityLayerMask"] = component.visibilityLayerMask;
}

bool Engine::IsEntityActiveInHierarchy(ECSWorld& world, const Entity& entity) {

	if (const SceneObjectComponent* sceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(entity)) {
		return sceneObject->activeInHierarchy;
	}
	return world.IsAlive(entity);
}