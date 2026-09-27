#include "EntitySnapshotPlacementTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/EntitySnapshotPlacement.h>

bool NEMTests::TestEntitySnapshotPlacement() {

	using namespace Engine;
	const AssetID oldAsset{ 1, 2 }, newAsset{ 3, 4 }, prefabAsset{ 5, 6 };
	EntityTreeSnapshot snapshot;
	snapshot.rootStableUUID = Engine::UUID{ 1 };
	snapshot.ownerSceneInstanceID = Engine::UUID{ 10 };
	snapshot.ownerSourceAsset = oldAsset;
	for (uint64_t index = 1; index <= 3; ++index) {
		SerializedEntitySnapshot entity;
		entity.stableUUID = Engine::UUID{ index };
		entity.sceneInstanceID = snapshot.ownerSceneInstanceID;
		entity.sourceAsset = index == 3 ? prefabAsset : oldAsset;
		entity.components["SceneObject"]["localFileId"] = ToString(Engine::UUID{ index * 100 });
		if (index == 3) {
			entity.components["PrefabLink"] = nlohmann::json::object();
		}
		snapshot.entities.emplace_back(std::move(entity));
	}
	const auto reference = [](AssetID asset, uint64_t id) {
		return nlohmann::json{ { "kind", "Scene" }, { "sourceAsset", ToString(asset) },
			{ "localFileId", ToString(Engine::UUID{ id }) } };
	};
	snapshot.entities[0].components["Script"] = nlohmann::json::array({ { { "serializedFields", {
		{ "internal", reference(oldAsset, 200) }, { "external", reference(oldAsset, 500) },
		{ "prefab", reference(prefabAsset, 300) }, { "otherAsset", reference(prefabAsset, 200) }
	} } } });
	snapshot.entities[0].referenceTargets.push_back({ oldAsset, Engine::UUID{ 200 }, Engine::UUID{ 2 } });
	std::vector<EntityTreeSnapshot> snapshots{ snapshot };
	const std::vector<EntitySnapshotScene> destinations{ { Engine::UUID{ 20 }, newAsset } };
	EntitySnapshotPlacement::Apply(snapshots, destinations);
	const auto& result = snapshots[0];
	const auto& fields = result.entities[0].components["Script"][0]["serializedFields"];
	return result.ownerSceneInstanceID == destinations[0].instanceID && result.ownerSourceAsset == newAsset &&
		result.entities[1].sceneInstanceID == destinations[0].instanceID && result.entities[1].sourceAsset == newAsset &&
		result.entities[2].sceneInstanceID == destinations[0].instanceID && result.entities[2].sourceAsset == prefabAsset &&
		fields["internal"] == reference(newAsset, 200) && fields["external"] == reference(oldAsset, 500) &&
		fields["prefab"] == reference(prefabAsset, 300) && fields["otherAsset"] == reference(prefabAsset, 200) &&
		result.entities[0].referenceTargets[0].sourceAsset == newAsset;
}
