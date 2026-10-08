#include "TestContracts.h"
#include "TestFixtures.h"
#include "SceneCreationLifetimeTests.h"
#include "SystemContextLifetimeTests.h"

#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "SceneStorageTests.h"
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetCopySnapshot.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

namespace {

	// 中断したphaseと後処理を確認するSystem
	class UpdateInterruptionProbe final : public Engine::ISystem {
	public:
		UpdateInterruptionProbe(bool& interrupted, std::array<std::array<uint32_t, 3>, 2>& counts, uint32_t index,
			int32_t phase, Engine::Entity changed, Engine::Entity destroyed)
			: interrupted_(interrupted), counts_(counts), index_(index), phase_(phase), changed_(changed),
			  destroyed_(destroyed) {}

		void FixedUpdate(Engine::ECSWorld& world, Engine::SystemContext&) override { Invoke(world, 0); }
		void Update(Engine::ECSWorld& world, Engine::SystemContext&) override { Invoke(world, 1); }
		void LateUpdate(Engine::ECSWorld& world, Engine::SystemContext&) override { Invoke(world, 2); }
		const char* GetName() const override { return "UpdateInterruptionProbe"; }
		void Resume() { phase_ = -1; }

	private:
		// callback後に立てる中断要求
		bool& interrupted_;
		// Systemとphaseごとの呼出回数
		std::array<std::array<uint32_t, 3>, 2>& counts_;
		uint32_t index_;
		int32_t phase_;
		// 中断前に予約する変更対象
		Engine::Entity changed_;
		Engine::Entity destroyed_;

		void Invoke(Engine::ECSWorld& world, uint32_t phase) {

			++counts_[index_][phase];
			if (phase_ != static_cast<int32_t>(phase)) {
				return;
			}
			world.GetCommandBuffer().EnqueueRemoveComponentByName(changed_, "Name");
			world.DestroyEntity(destroyed_);
			interrupted_ = true;
		}
	};

	bool CheckSchedulerInterruption() {

		for (int32_t phase = 0; phase < 3; ++phase) {
			Engine::ECSWorld world;
			const auto changed = world.CreateEntity();
			world.AddComponent<Engine::NameComponent>(changed).name = "Interrupted";
			const auto destroyed = world.CreateEntity();
			bool interrupted = false;
			std::array<std::array<uint32_t, 3>, 2> counts{};
			Engine::SystemContext context{};
			context.world = &world;
			context.deltaTime = 0.01f;
			context.updateInterruption = [&] { return interrupted; };
			Engine::SystemScheduler scheduler;
			scheduler.SetFixedDeltaTime(0.01f);
			auto first = std::make_unique<UpdateInterruptionProbe>(interrupted, counts, 0, phase, changed, destroyed);
			auto* firstSystem = first.get();
			scheduler.AddSystem(std::move(first), 0);
			scheduler.AddSystem(std::make_unique<UpdateInterruptionProbe>(interrupted, counts, 1, -1, changed, destroyed), 1);
			scheduler.Tick(&world, context);
			// 予約処理を確定してから残りの更新を止める
			if (!interrupted || world.HasComponent<Engine::NameComponent>(changed) || world.IsAlive(destroyed)) {
				return false;
			}
			for (int32_t current = 0; current < 3; ++current) {
				if (counts[0][current] != (current <= phase ? 1u : 0u) || counts[1][current] != (current < phase ? 1u : 0u)) {
					return false;
				}
			}
			const auto before = counts;
			firstSystem->Resume();
			interrupted = false;
			scheduler.Tick(&world, context);
			// Resumeで次のcallbackへ進み、固定更新の負債を持ち越さない
			for (uint32_t index = 0; index < 2; ++index) {
				for (uint32_t current = 0; current < 3; ++current) {
					if (counts[index][current] != before[index][current] + 1) {
						return false;
					}
				}
			}
			scheduler.DetachCurrentWorld(context);
		}
		return true;
	}
}

namespace NEMTests {

	bool TestSceneAssetCopy() {

		TestDirectory directory("SceneCopy", Engine::RuntimePaths::GetGameAssetsRoot());
		const auto& testRoot = directory.GetPath();
		std::filesystem::create_directories(testRoot / "Folder");
		Engine::ECSWorld world;
		const Engine::Entity parent = Engine::SceneAuthoring::CreateGameObject(world, "Parent");
		const Engine::Entity child = Engine::SceneAuthoring::CreateGameObject(world, "Child");
		Engine::HierarchySystem hierarchy;
		hierarchy.SetParent(world, child, parent);
		const Engine::UUID parentID = world.GetComponent<Engine::SceneObjectComponent>(parent).localFileID;
		const Engine::UUID childID = world.GetComponent<Engine::SceneObjectComponent>(child).localFileID;
		Engine::SceneSystem system;
		std::vector<std::filesystem::path> actorRoots;
		bool passed = true;
		std::string error;

		// 両方の保存形式で参照と階層を維持した独立コピーを確認する
		for (bool external : {false, true}) {

			const std::filesystem::path source = testRoot / (external ? "External.scene.json" : "Single.scene.json");
			const std::filesystem::path target = testRoot / "Folder" / source.filename();
			Engine::AssetMeta meta{};
			meta.guid = Engine::AssetGUID::New();
			meta.type = Engine::AssetType::Scene;
			const Engine::AssetID otherAsset = Engine::AssetGUID::New();
			const nlohmann::json selfReference = {
				{"kind", "Scene"},
				{"sourceAsset", Engine::ToString(meta.guid)},
				{"localFileId", Engine::ToString(childID)},
			};
			nlohmann::json otherReference = selfReference;
			otherReference["sourceAsset"] = Engine::ToString(otherAsset);
			nlohmann::json implicitReference = selfReference;
			implicitReference["sourceAsset"] = "";
			nlohmann::json prefabReference = selfReference;
			prefabReference["kind"] = "Prefab";
			nlohmann::json root = {
				{"SchemaVersion", 3},
				{"Header", Engine::ToJson(Engine::SceneHeader{})},
				{"Entities", system.SerializeEntities(world)},
				{"PrefabInstances", nlohmann::json::array()},
				{"CopyReferences", {selfReference, otherReference, implicitReference, prefabReference}},
			};
			// 任意のシリアライズ領域でも型付き参照だけを書き換える
			root["Entities"][0]["CopyReference"] = selfReference;
			root["Header"]["sharedAsset"] = Engine::ToString(meta.guid);
			passed &= Engine::AssetDatabase::WriteMetaFile(source.wstring() + L".meta", meta) &&
					  Engine::SceneSystem::WriteSaveSnapshot({source, meta.guid, root, external});
			const std::filesystem::path sourceActors =
				Engine::RuntimePaths::GetGameAssetsRoot() / "ExternalActors" / Engine::ToString(meta.guid);
			actorRoots.push_back(sourceActors);
			const nlohmann::json sourceBefore = Engine::JsonAdapter::Load(source, false);
			passed &= Engine::SceneSystem::CopySceneAssets({{source, target}}, error);
			Engine::AssetMeta copiedMeta{};
			passed &= Engine::AssetDatabase::ReadMetaFile(target.wstring() + L".meta", copiedMeta) && copiedMeta.guid &&
					  copiedMeta.guid != meta.guid;
			const std::filesystem::path copiedActors =
				Engine::RuntimePaths::GetGameAssetsRoot() / "ExternalActors" / Engine::ToString(copiedMeta.guid);
			actorRoots.push_back(copiedActors);
			const nlohmann::json copied = Engine::JsonAdapter::Load(target, false);
			if (!copied.is_object()) {
				passed = false;
				break;
			}
			passed &= copied.contains("ExternalActors") == external &&
					  copied["CopyReferences"][0]["sourceAsset"] == Engine::ToString(copiedMeta.guid) &&
					  copied["CopyReferences"][1] == otherReference && copied["CopyReferences"][2] == implicitReference &&
					  copied["CopyReferences"][3] == prefabReference &&
					  copied["Header"]["sharedAsset"] == Engine::ToString(meta.guid);
			nlohmann::json expected = sourceBefore;
			expected["Header"]["name"] = external ? "External" : "Single";
			expected["CopyReferences"][0]["sourceAsset"] = Engine::ToString(copiedMeta.guid);
			if (!external) {
				expected["Entities"][0]["CopyReference"]["sourceAsset"] = Engine::ToString(copiedMeta.guid);
			}
			passed &= copied == expected;
			Engine::ECSWorld loaded;
			std::vector<Engine::Entity> entities;
			passed &= system.LoadScene(target, loaded, nullptr, copiedMeta.guid, Engine::UUID{500}, nullptr, &entities) &&
					  entities.size() == 2;
			bool childFound = false;
			for (Engine::Entity entity : entities) {

				if (loaded.GetComponent<Engine::SceneObjectComponent>(entity).localFileID == childID) {
					childFound = loaded.GetComponent<Engine::HierarchyComponent>(entity).parentLocalFileID == parentID;
				}
			}
			passed &= childFound;
			passed &= !Engine::SceneSystem::CopySceneAssets({{source, target}}, error) &&
					  Engine::JsonAdapter::Load(target, false) == copied;
			if (external) {

				const std::string actorName = copied["ExternalActors"][0].get<std::string>() + ".actor.json";
				nlohmann::json copiedActor = Engine::JsonAdapter::Load(copiedActors / actorName, false);
				nlohmann::json expectedActor = Engine::JsonAdapter::Load(sourceActors / actorName, false);
				const nlohmann::json originalActor = expectedActor;
				expectedActor["CopyReference"]["sourceAsset"] = Engine::ToString(copiedMeta.guid);
				passed &= copiedActor == expectedActor;
				copiedActor["Components"]["Name"]["name"] = "Changed";
				passed &= Engine::JsonAdapter::SaveCanonical(copiedActors / actorName, copiedActor) &&
						  Engine::JsonAdapter::Load(sourceActors / actorName, false) == originalActor;
				// Actor欠損時はバッチ全体を作成しない
				std::filesystem::remove(sourceActors / actorName);
				const std::filesystem::path rejected = testRoot / "Rejected.scene.json";
				const std::filesystem::path first = testRoot / "First.scene.json";
				passed &= !Engine::SceneSystem::CopySceneAssets(
							  {{testRoot / "Single.scene.json", first}, {source, rejected}}, error) &&
						  !std::filesystem::exists(first) && !std::filesystem::exists(rejected) &&
						  !std::filesystem::exists(rejected.wstring() + L".meta");
			}
			passed &= Engine::JsonAdapter::Load(source, false) == sourceBefore;
		}
		// 空シーンと日本語ファイル名も同じ形式で複製する
		for (bool external : {false, true}) {

			const auto source = testRoot / (external ? L"空の外部.scene.json" : L"空の単一.scene.json");
			const auto target = testRoot / "Folder" / source.filename();
			Engine::AssetMeta meta{};
			meta.type = Engine::AssetType::Scene;
			meta.guid = Engine::AssetGUID::New();
			const nlohmann::json root = {
				{"SchemaVersion", 3},
				{"Header", Engine::ToJson(Engine::SceneHeader{})},
				{"Entities", nlohmann::json::array()},
				{"PrefabInstances", nlohmann::json::array()},
			};
			passed &= Engine::AssetDatabase::WriteMetaFile(source.wstring() + L".meta", meta) &&
					  Engine::SceneSystem::WriteSaveSnapshot({source, meta.guid, root, external}) &&
					  Engine::SceneSystem::CopySceneAssets({{source, target}}, error);
			Engine::AssetMeta copiedMeta{};
			passed &= Engine::AssetDatabase::ReadMetaFile(target.wstring() + L".meta", copiedMeta);
			Engine::ECSWorld loaded;
			std::vector<Engine::Entity> created;
			passed &= system.LoadScene(target, loaded, nullptr, copiedMeta.guid, Engine::UUID{501}, nullptr, &created) &&
					  created.empty() && Engine::JsonAdapter::Load(target, false).contains("ExternalActors") == external;
			// JSONの型が壊れている場合も例外を外へ出さず複製を中止する
			nlohmann::json invalid = root;
			invalid["SchemaVersion"] = "invalid";
			const auto rejected = testRoot / "Invalid.scene.json";
			passed &= Engine::JsonAdapter::SaveCanonical(source, invalid) &&
					  !Engine::SceneSystem::CopySceneAssets({{source, rejected}}, error) &&
					  !std::filesystem::exists(rejected) && !std::filesystem::exists(rejected.wstring() + L".meta");
			actorRoots.push_back(Engine::RuntimePaths::GetGameAssetsRoot() / "ExternalActors" / Engine::ToString(meta.guid));
			actorRoots.push_back(
				Engine::RuntimePaths::GetGameAssetsRoot() / "ExternalActors" / Engine::ToString(copiedMeta.guid));
		}
		// 後続metaの準備失敗で先行Sceneも公開しない
		Engine::SceneAssetStorage storage;
		std::vector<Engine::SceneAssetCopySnapshot> copies(2);
		for (size_t index = 0; index < copies.size(); ++index) {

			auto& copy = copies[index];
			copy.meta.type = Engine::AssetType::Scene;
			copy.meta.guid = Engine::AssetGUID::New();
			copy.snapshot.sceneAsset = copy.meta.guid;
			copy.snapshot.scenePath = testRoot / ("Batch" + std::to_string(index) + ".scene.json");
			copy.snapshot.useExternalActors = true;
			copy.snapshot.root = {{"SchemaVersion", 3}, {"Header", Engine::ToJson(Engine::SceneHeader{})},
				{"Entities", system.SerializeEntities(world)}, {"PrefabInstances", nlohmann::json::array()}};
			actorRoots.push_back(Engine::SceneAssetStorage::ResolveActorRoot(copy.snapshot.scenePath, copy.meta.guid));
		}
		copies[1].meta.importerSettings = {{"text", std::string(1, static_cast<char>(0xff))}};
		passed &= !storage.CreateCopies(copies, error) && !error.empty();
		for (const auto& copy : copies) {

			passed &=
				!std::filesystem::exists(copy.snapshot.scenePath) &&
				!std::filesystem::exists(copy.snapshot.scenePath.wstring() + L".meta") &&
				!std::filesystem::exists(Engine::SceneAssetStorage::ResolveActorRoot(copy.snapshot.scenePath, copy.meta.guid));
		}
		copies[1].meta.importerSettings = nlohmann::json::object();
		passed &= storage.CreateCopies(copies, error);
		bool batchRecorded = false;
		for (const auto& recovery : storage.GetRecoveries()) {

			const nlohmann::json operation = Engine::JsonAdapter::Load(recovery / "operation.json", false);
			if (operation.value("label", std::string{}) != "シーン複製" ||
				operation.value("state", std::string{}) != "completed" || operation["files"].size() != 8) {
				continue;
			}
			for (const auto& file : operation["files"]) {
				if (file["path"] == Engine::Algorithm::PathToUTF8(copies[0].snapshot.scenePath)) {
					batchRecorded = true;
				}
			}
		}
		passed &= batchRecorded;
		std::error_code ec;
		directory.Remove();
		for (const auto& actorRoot : actorRoots) {

			std::filesystem::remove_all(actorRoot, ec);
		}
		return passed;
	}

	bool TestSceneLifecycleContext() {

		if (!CheckSystemContextLifetime()) {
			return false;
		}
		if (!CheckSceneCreationLifetime()) {
			return false;
		}
		if (!CheckSchedulerInterruption()) {
			return false;
		}

		Engine::ECSWorld world;
		Engine::AssetDatabase database;
		Engine::SceneSystem sceneSystem;
		Engine::SceneInstanceManager scenes;
		Engine::SceneHeader firstHeader{};
		firstHeader.name = "First";
		const Engine::UUID firstScene = scenes.CreateScratchScene(firstHeader);
		Engine::SceneHeader secondHeader{};
		secondHeader.name = "Second";
		const Engine::UUID secondScene = scenes.CreateScratchScene(secondHeader);
		scenes.SetActive(firstScene);

		Engine::WorldCommandServices services{};
		services.assetDatabase = &database;
		services.sceneInstances = &scenes;
		services.sceneSystem = &sceneSystem;
		world.SetCommandServices(services);

		Engine::SystemContext context{};
		context.world = &world;
		context.assetDatabase = &database;
		context.mode = Engine::WorldMode::Play;
		context.SetActiveSceneHeader(&scenes.GetActive()->header);

		auto observer = std::make_unique<SceneContextObserverSystem>();
		SceneContextObserverSystem* observerPtr = observer.get();
		Engine::SystemScheduler scheduler;
		scheduler.AddSystem(std::move(observer), 0);
		scheduler.Tick(&world, context);

		// 計測を止めてもSceneの安全地点とLifecycle通知を維持する
		auto& profiler = Engine::FrameProfiler::GetInstance();
		const bool previousProfiling = profiler.IsEnabled();
		profiler.SetEnabled(false);
		world.GetCommandBuffer().EnqueueUnloadScene(firstScene);
		scheduler.Tick(&world, context);
		profiler.SetEnabled(previousProfiling);

		const Engine::SceneInstance* activeScene = scenes.GetActive();
		return activeScene && activeScene->instanceID == secondScene &&
			   observerPtr->observedHeader == context.GetActiveSceneHeader() &&
			   context.GetActiveSceneHeader()->name == "Second" && observerPtr->notificationCount == 1;
	}
}
