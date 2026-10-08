#include "PrefabInputTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>

bool NEMTests::TestPrefabInputFailures() {

	using namespace Engine;
	PrefabInstanceData original;
	original.prefabAsset = AssetID{ 1, 2 };
	original.instanceID = Engine::UUID{ 10 };
	original.entityMap = { { Engine::UUID{ 100 }, Engine::UUID{ 200 } } };
	original.modifications.push_back({ Engine::UUID{ 100 }, "Name/name", "Retained" });
	const auto saved = ToJson(original);
	PrefabInstanceData result;
	if (!FromJson(saved, result)) {
		return false;
	}
	for (const char* key : { "EntityMap", "Modifications", "AddedComponents", "RemovedComponents", "HierarchyMods",
		"RemovedEntities", "AddedEntities", "AddedEntityMap", "NestedInstances", "RemovedNestedSlots" }) {
		auto invalid = saved;
		invalid[key] = nlohmann::json::object();
		if (FromJson(invalid, result) || ToJson(result) != saved) {
			return false;
		}
	}
	for (const char* key : { "PrefabAsset", "InstanceID", "RootParent", "NestedSlotID" }) {
		auto invalid = saved;
		invalid[key] = 123;
		if (FromJson(invalid, result) || ToJson(result) != saved) {
			return false;
		}
	}
	auto invalid = saved;
	invalid["NestedInstances"] = nlohmann::json::array({ nullptr });
	if (FromJson(invalid, result) || ToJson(result) != saved) {
		return false;
	}
	invalid = saved;
	invalid["EntityMap"].push_back(invalid["EntityMap"][0]);
	return !FromJson(invalid, result) && ToJson(result) == saved;
}
