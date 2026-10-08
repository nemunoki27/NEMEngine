#include "PrefabCreationPublicationTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Assets/Project/ProjectAssetOperations.h>
#include <Engine/Editor/Assets/Project/ProjectAssetFileUtility.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetMetaStorage.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonCanonical.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>

// c++
#include <functional>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {

	// 保存中の失敗と外部変更を発生させる
	struct PrefabSaveProbe {

		static inline std::function<void()> action;
		int value = 29;
	};

	void SerializeComponent([[maybe_unused]] const Engine::ECSWorld& world, [[maybe_unused]] const Engine::Entity& entity,
		const PrefabSaveProbe& value, nlohmann::json& out) {

		out = value.value;
		if (PrefabSaveProbe::action) {
			PrefabSaveProbe::action();
		}
	}

	void DeserializeComponent([[maybe_unused]] Engine::ECSWorld& world, [[maybe_unused]] const Engine::Entity& entity,
		const nlohmann::json& in, PrefabSaveProbe& value) {

		value.value = in.get<int>();
	}

	// 作成先の決定だけではファイルを公開しない
	bool CheckPlanAndCreation() {

		using namespace Engine;
		NEMTests::TestDirectory directory("PrefabCreation", RuntimePaths::GetGameAssetsRoot());
		const auto logical = RuntimePaths::ToAssetPath(directory.GetPath());
		const auto plan =
			ProjectAssetFileUtility::PlanCreate(ProjectAssetSource::Game, logical, ProjectAssetFileKind::Prefab, "Root");
		if (!plan.success || !std::filesystem::is_empty(directory.GetPath())) {
			return false;
		}
		AssetDatabase database;
		database.Init();
		ECSWorld world;
		const auto root = SceneAuthoring::CreateGameObject(world, "Root");
		const auto child = SceneAuthoring::CreateGameObject(world, "Child");
		HierarchySystem hierarchy;
		hierarchy.SetParent(world, child, root);
		ProjectAssetFileResult result;
		if (!ProjectAssetOperations::SavePrefab(database, world, root, ProjectAssetSource::Game, logical, result) ||
			!result.success || result.fullPath != plan.fullPath) {
			return false;
		}
		const auto* indexed = database.FindByPath(result.assetPath);
		nlohmann::json data;
		AssetMeta metadata;
		if (!indexed || !JsonFile::TryLoad(result.fullPath, data) || data["Entities"].size() != 2 ||
			!AssetMetaStorage::ReadMetaFile(AssetMetaStorage::MetaPathOf(result.fullPath), metadata) ||
			metadata.guid != indexed->guid ||
			data["Header"]["rootLocalFileID"] != ToString(world.GetComponent<SceneObjectComponent>(root).localFileID) ||
			world.GetComponent<PrefabLinkComponent>(root).prefabAsset != metadata.guid) {
			return false;
		}
		if (StorageFileUtility::ReadVerifiedBytes(result.fullPath, StorageFileUtility::FileRevision(result.fullPath)) !=
			JsonCanonical::SerializeCanonical(data, 4)) {
			return false;
		}
		const auto firstID = metadata.guid;
		ECSWorld instantiated;
		PrefabSystem prefabs;
		PrefabInstantiateResult spawned;
		if (!prefabs.InstantiatePrefab(database, hierarchy, instantiated, firstID, spawned) ||
			spawned.createdEntities.size() != 2) {
			return false;
		}
		// 同名の再作成でも既存Prefabを置き換えない
		ProjectAssetFileResult second;
		if (!ProjectAssetOperations::SavePrefab(database, world, root, ProjectAssetSource::Game, logical, second) ||
			second.fullPath != directory.GetPath() / "Root 1.prefab.json") {
			return false;
		}
		const auto* next = database.FindByPath(second.assetPath);
		return next && next->guid != firstID && database.Find(firstID) &&
			   AssetMetaStorage::ReadMetaFile(AssetMetaStorage::MetaPathOf(result.fullPath), metadata) &&
			   metadata.guid == firstID;
	}

	// 保存失敗と外部作成で本体とmetaと索引を部分公開しない
	bool CheckCreationFailure(uint32_t mode) {

		using namespace Engine;
		NEMTests::TestDirectory directory("PrefabCreationFailure", RuntimePaths::GetGameAssetsRoot());
		const auto logical = RuntimePaths::ToAssetPath(directory.GetPath());
		const auto target = directory.GetPath() / "Blocked.prefab.json";
		AssetDatabase database;
		database.Init();
		auto world = std::make_unique<ECSWorld>();
		const auto root = SceneAuthoring::CreateGameObject(*world, "Blocked");
		world->AddComponent<PrefabSaveProbe>(root);
		const auto lifetime = world->GetLifetime();
		const ScopedCleanup cleanup([]() noexcept { PrefabSaveProbe::action = {}; });
		if (mode == 0) {
			PrefabSaveProbe::action = [] { throw std::runtime_error("Prefab save failure"); };
		} else if (mode == 1) {
			PrefabSaveProbe::action = [&] { std::ofstream(target) << "foreign"; };
		} else if (mode == 2) {
			PrefabSaveProbe::action = [&] { world.reset(); };
		} else {
			const auto child = SceneAuthoring::CreateGameObject(*world, "Duplicate");
			HierarchySystem hierarchy;
			hierarchy.SetParent(*world, child, root);
			world->GetComponent<SceneObjectComponent>(child).localFileID =
				world->GetComponent<SceneObjectComponent>(root).localFileID;
		}
		ProjectAssetFileResult result;
		const auto databaseRevision = database.GetStructureRevision();
		bool ended = false;
		bool saved = false;
		try {
			saved = ProjectAssetOperations::SavePrefab(database, *world, root, ProjectAssetSource::Game, logical, result);
		} catch (const std::runtime_error&) {
			ended = mode == 2 && !lifetime->IsAlive();
		}
		if (saved || result.success || database.GetStructureRevision() != databaseRevision ||
			database.FindByPath(RuntimePaths::ToAssetPath(target)) ||
			std::filesystem::exists(AssetMetaStorage::MetaPathOf(target)) || ended != (mode == 2)) {
			return false;
		}
		if (mode == 1) {
			return StorageFileUtility::ReadVerifiedBytes(target, StorageFileUtility::FileRevision(target)) == "foreign";
		}
		return !std::filesystem::exists(target) && std::filesystem::is_empty(directory.GetPath());
	}

	// 通常保存も失敗時にmetaと索引を残さず再試行できる
	bool CheckCoreSaveRetry() {

		using namespace Engine;
		NEMTests::TestDirectory directory("PrefabCoreSaveRetry", RuntimePaths::GetGameAssetsRoot());
		const auto target = directory.GetPath() / "Retry.prefab.json";
		const auto logical = RuntimePaths::ToAssetPath(target);
		AssetDatabase database;
		database.Init();
		ECSWorld world;
		const auto root = SceneAuthoring::CreateGameObject(world, "Retry");
		world.AddComponent<PrefabSaveProbe>(root);
		const ScopedCleanup cleanup([]() noexcept { PrefabSaveProbe::action = {}; });
		PrefabSaveProbe::action = [] { throw std::runtime_error("Prefab retry failure"); };
		PrefabSystem prefabs;
		const auto revision = database.GetStructureRevision();
		bool failed = false;
		try {
			failed = !prefabs.SavePrefab(database, world, root, logical);
		} catch (const std::runtime_error&) {
			failed = true;
		}
		if (!failed || database.GetStructureRevision() != revision || database.FindByPath(logical) ||
			!std::filesystem::is_empty(directory.GetPath())) {
			return false;
		}
		PrefabSaveProbe::action = {};
		if (!prefabs.SavePrefab(database, world, root, logical)) {
			return false;
		}
		nlohmann::json data;
		AssetMeta meta;
		const auto* indexed = database.FindByPath(logical);
		if (!indexed || !JsonFile::TryLoad(target, data) ||
			!AssetMetaStorage::ReadMetaFile(AssetMetaStorage::MetaPathOf(target), meta) || meta.guid != indexed->guid ||
			StorageFileUtility::ReadVerifiedBytes(target, StorageFileUtility::FileRevision(target)) !=
				JsonCanonical::SerializeCanonical(data, 4)) {
			return false;
		}
		const auto firstID = meta.guid;
		if (!prefabs.SavePrefab(database, world, root, logical)) {
			return false;
		}
		indexed = database.FindByPath(logical);
		return indexed && indexed->guid == firstID;
	}

	// ファイルを作成先として指定しても内容を変更しない
	bool CheckInvalidDirectory() {

		using namespace Engine;
		NEMTests::TestDirectory directory("PrefabInvalidDirectory", RuntimePaths::GetGameAssetsRoot());
		const auto target = directory.GetPath() / "Occupied";
		std::ofstream(target) << "preserved";
		const auto revision = StorageFileUtility::FileRevision(target);
		const auto logical = RuntimePaths::ToAssetPath(target);
		const auto plan =
			ProjectAssetFileUtility::PlanCreate(ProjectAssetSource::Game, logical, ProjectAssetFileKind::Prefab, "Blocked");
		const auto created =
			ProjectAssetFileUtility::Create(ProjectAssetSource::Game, logical, ProjectAssetFileKind::Prefab, "Blocked");
		return !plan.success && !plan.message.empty() && !created.success && !created.message.empty() &&
			   StorageFileUtility::FileRevision(target) == revision &&
			   StorageFileUtility::ReadVerifiedBytes(target, revision) == "preserved";
	}
}

bool NEMTests::TestPrefabCreationPublication() {

	RegisterTestComponents();
	auto& registry = Engine::ComponentTypeRegistry::GetInstance();
	registry.Register<PrefabSaveProbe>(registry.GetComponentTypeCount(), "PrefabSaveProbe");
	if (!CheckPlanAndCreation() || !CheckCoreSaveRetry() || !CheckInvalidDirectory()) {
		return false;
	}
	for (uint32_t mode = 0; mode < 4; ++mode) {
		if (!CheckCreationFailure(mode)) {
			return false;
		}
	}
	return true;
}
