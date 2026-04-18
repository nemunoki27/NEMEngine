#include "SceneObjectComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/World/ECSWorld.h>

//============================================================================
//	SceneObjectComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, SceneObjectComponent& component) {

	const std::string localID = in.value("localFileID", "");
	component.localFileID = localID.empty() ? UUID::New() : FromString16Hex(localID);
	component.activeSelf = in.value("activeSelf", true);
	component.visibilityLayerMask = in.value("visibilityLayerMask", 0xFFFFFFFFu);

	// ランタイム使用用のIDはシリアライズされないため、初期化しておく
	component.sourceAsset = AssetID{};
	component.sceneInstanceID = UUID{};
	component.activeInHierarchy = component.activeSelf;
}

void Engine::to_json(nlohmann::json& out, const SceneObjectComponent& component) {

	out["localFileID"] = component.localFileID ? ToString(component.localFileID) : "";
	out["activeSelf"] = component.activeSelf;
	out["visibilityLayerMask"] = component.visibilityLayerMask;
}

bool Engine::IsEntityActiveInHierarchy(ECSWorld& world, const Entity& entity) {

	if (!world.IsAlive(entity)) {
		return false;
	}
	if (!world.HasComponent<Engine::SceneObjectComponent>(entity)) {
		return true;
	}
	return world.GetComponent<Engine::SceneObjectComponent>(entity).activeInHierarchy;
}