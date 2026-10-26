#include "SceneEntityReference.h"

//============================================================================
//	include
//============================================================================
#include "SceneObjectUtility.h"
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>

using namespace Engine;

SceneEntityReference SceneEntityReference::FromEntity(const ECSWorld& world, Entity entity, bool prefab) {

	const auto* scene = world.TryGetComponent<SceneObjectComponent>(entity);
	return scene ? SceneEntityReference{scene->sourceAsset, scene->localFileID, prefab} : SceneEntityReference{};
}

Entity SceneEntityReference::Resolve(const ECSWorld& world, Entity owner) const {

	if (!localFileID) return Entity::Null();
	const UUID instance = SceneObjectUtility::GetSceneInstanceID(world, owner);
	const auto* scene = world.TryGetComponent<SceneObjectComponent>(owner);
	if (!sourceAsset || (scene && scene->sourceAsset == sourceAsset)) {
		const Entity target = SceneObjectUtility::FindByLocalFileID(world, instance, localFileID);
		const auto* targetScene = world.TryGetComponent<SceneObjectComponent>(target);
		return !sourceAsset || (targetScene && targetScene->sourceAsset == sourceAsset) ? target : Entity::Null();
	}
	// 別Assetの同じ文書内IDでは補わない
	return SceneObjectUtility::ResolveReference(world, sourceAsset, localFileID, instance);
}

void Engine::from_json(const nlohmann::json& in, SceneEntityReference& reference) {

	AssetID asset{};
	UUID local{};
	if (!PrefabReferenceRemapper::TryReadEntityReference(in, asset, local)) {
		reference = {};
		return;
	}
	reference = {asset, local, in.value("kind", "Scene") == "Prefab"};
}

void Engine::to_json(nlohmann::json& out, const SceneEntityReference& reference) {

	out = {{"kind", reference.prefab ? "Prefab" : "Scene"},
		{"sourceAsset", reference.sourceAsset ? ToString(reference.sourceAsset) : std::string{}},
		{"localFileId", reference.localFileID ? ToString(reference.localFileID) : std::string{}}};
}
