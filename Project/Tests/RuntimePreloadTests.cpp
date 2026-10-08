#include "RuntimePreloadTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Runtime/Application/RuntimeAssetPreloadPlan.h>
#include <Engine/Core/Runtime/Application/RuntimeAssetPreloadRequests.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <array>

bool NEMTests::TestRuntimePreload() {

	using namespace Engine;
	TestDirectory directory("RuntimePreload", RuntimePaths::GetGameAssetsRoot());
	AssetDatabase database;
	if (!database.Init()) { return false; }
	std::array<AssetID, 3> scenes;
	for (size_t i = 0; i < scenes.size(); ++i) {
		auto path = directory.GetPath() / (std::to_string(i) + ".scene.json");
		if (!JsonFile::Save(path, nlohmann::json::object())) { return false; }
		scenes[i] = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Scene);
		if (!scenes[i]) { return false; }
	}
	AssetID missing = AssetID::New();
	// 循環と欠損を持つSceneでも独立したSceneは収集しない
	for (size_t i = 0; i < 2; ++i) {
		auto path = database.ResolveFullPath(scenes[i]);
		if (!JsonFile::Save(path, { { "scene", ToString(scenes[1 - i]) }, { "mesh", ToString(missing) } }) ||
			!database.RefreshDependencies(scenes[i])) { return false; }
	}
	auto plan = RuntimeAssetPreloadPlan::Collect(database, std::span<const AssetID>(scenes.data(), 1));
	if (plan.assets.size() != 2 || plan.missing.size() != 1 || plan.missing.front() != missing ||
		std::find(plan.assets.begin(), plan.assets.end(), scenes[2]) != plan.assets.end()) { return false; }
	RuntimeAssetPreloadRequests requests;
	requests.Add(scenes[0]);
	requests.Add(scenes[0]);
	requests.Add({});
	auto roots = requests.Take();
	return roots.size() == 1 && roots.front() == scenes[0] && requests.Take().empty();
}
