#include "SceneCreationLifetimeTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Serialization/SceneCreationScope.h>
#include <Engine/Core/World/Scene/Serialization/SceneInstantiator.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabInstantiator.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabInstanceRebuilder.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabHeader.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabDocument.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>

// c++
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace Engine;

namespace {

	// 読込と破棄の通知で接続が変わったら残りのSceneを保持する
	bool CheckCommandConnection(AssetDatabase& database, SceneSystem& system, AssetID sceneID) {

		for (bool disconnectOnUnload : {false, true}) {
			for (uint32_t mode = 0; mode < 5; ++mode) {
				ECSWorld world;
				SceneInstanceManager scenes;
				SceneInstanceManager replacement;
				const auto replacementID = replacement.CreateScratchScene(SceneHeader{});
				std::vector<Engine::UUID> previous;
				for (uint32_t index = 0; index < 3; ++index) {
					const auto id = scenes.CreateScratchScene(SceneHeader{});
					previous.push_back(id);
					const auto entity = SceneAuthoring::CreateGameObject(world, "Previous");
					world.GetComponent<SceneObjectComponent>(entity).sceneInstanceID = id;
					scenes.Find(id)->createdEntities.push_back(entity);
				}
				scenes.SetActive(previous.front());
				const WorldCommandServices services{&database, &scenes, &system};
				world.SetCommandServices(services);
				const auto revision = world.GetCommandServiceRevision();
				struct ConnectionMutation {

					WorldCommandServices services;	   // 開始時の接続
					SceneInstanceManager& replacement; // 変更後の接続
					uint32_t mode;					   // 接続の変更方法
					ComponentMutationKind kind;		   // 接続を変更する通知
					bool called = false;			   // 一度だけ接続を変更する
				} state{services, replacement, mode,
					disconnectOnUnload ? ComponentMutationKind::EntityDestroyed : ComponentMutationKind::EntityCreated};
				const auto listener = world.AddComponentMutationListener(
					[](ECSWorld& world, const Entity&, uint32_t, ComponentMutationKind kind, void* data) {
						auto& state = *static_cast<ConnectionMutation*>(data);
						if (state.called || kind != state.kind) {
							return;
						}
						state.called = true;
						if (state.mode == 1 || state.mode == 3) {
							world.SetCommandServices({});
						}
						if (state.mode == 2) {
							auto next = state.services;
							next.sceneInstances = &state.replacement;
							world.SetCommandServices(next);
						} else if (state.mode == 3 || state.mode == 4) {
							world.SetCommandServices(state.services);
						}
					},
					&state);
				const ScopedCleanup cleanup([&]() noexcept { world.RemoveComponentMutationListener(listener); });
				if (!scenes.TryBeginSingleLoadRequest()) {
					return false;
				}
				const auto nextID = Engine::UUID::New();
				world.GetCommandBuffer().EnqueueLoadSceneSingle(nextID, sceneID);
				world.FlushWorldCommands();
				const bool changed = mode > 0 && mode < 4;
				const size_t expected = changed ? (disconnectOnUnload ? 3 : 4) : 1;
				if (!state.called || scenes.GetAll().size() != expected || !scenes.Find(nextID) ||
					!scenes.TryBeginSingleLoadRequest() || replacement.GetAll().size() != 1 ||
					replacement.GetActive()->instanceID != replacementID ||
					(world.GetCommandServiceRevision() != revision) != changed ||
					(changed && !disconnectOnUnload && scenes.GetActive()->instanceID != previous.front())) {
					std::cerr << "Scene command connection failed: unload=" << disconnectOnUnload << " mode=" << mode << '\n';
					return false;
				}
			}
		}
		return true;
	}
}

bool NEMTests::CheckSceneCreationLifetime() {

	TestDirectory directory("SceneCreationLifetime", RuntimePaths::GetGameAssetsRoot());
	const auto scenePath = directory.GetPath() / "Fixture.scene.json";
	const auto prefabPath = directory.GetPath() / "Fixture.prefab.json";
	SceneSystem system;
	ECSWorld fixture;
	SceneAuthoring::CreateGameObject(fixture, "First");
	SceneAuthoring::CreateGameObject(fixture, "Second");
	const auto entities = system.SerializeEntities(fixture);
	nlohmann::json scene = {{"SchemaVersion", 3}, {"Header", ToJson(SceneHeader{})}, {"Entities", entities},
		{"PrefabInstances", nlohmann::json::array()}};
	PrefabHeader header;
	header.name = "Fixture";
	header.rootLocalFileID = FromString16Hex(entities[0]["LocalFileID"].get<std::string>());
	nlohmann::json prefab = {
		{"SchemaVersion", PrefabDocument::kPrefabSchemaVersion}, {"Header", ToJson(header)}, {"Entities", entities}};
	if (!JsonAdapter::SaveCanonical(scenePath, scene) || !JsonAdapter::SaveCanonical(prefabPath, prefab)) {
		return false;
	}
	AssetDatabase database;
	database.Init();
	const auto sceneID = database.ImportOrGet(RuntimePaths::ToAssetPath(scenePath), AssetType::Scene);
	const auto prefabID = database.ImportOrGet(RuntimePaths::ToAssetPath(prefabPath), AssetType::Prefab);
	if (!sceneID || !prefabID) {
		return false;
	}
	for (uint32_t mode = 0; mode < 10; ++mode) {
		for (bool failAfterEnding : {false, true}) {
			auto owned = std::make_unique<ECSWorld>();
			const auto lifetime = owned->GetLifetime();
			SceneInstanceManager scenes;
			HierarchySystem hierarchy;
			std::unique_ptr<SceneCreationScope> scope;
			if (mode < 2) {
				scope = std::make_unique<SceneCreationScope>(*owned);
				SceneAuthoring::CreateGameObject(*owned, "Generated");
			}
			struct EndingNotification {

				std::unique_ptr<ECSWorld>& owner; // 終了するWorldの所有元
				ComponentMutationKind kind;		  // 終了させる通知
				uint32_t threshold;				  // 終了までの通知数
				bool fail;						  // 終了後の例外送出
				uint32_t calls = 0;				  // 対象の通知数
			} state{owned, mode == 1 ? ComponentMutationKind::EntityDestroyed : ComponentMutationKind::EntityCreated,
				mode == 1 ? 1u : 2u, failAfterEnding};
			owned->AddComponentMutationListener(
				[](ECSWorld&, const Entity&, uint32_t, ComponentMutationKind kind, void* data) {
					auto& state = *static_cast<EndingNotification*>(data);
					if (kind == state.kind && ++state.calls == state.threshold) {
						state.owner.reset();
						if (state.fail) {
							throw std::runtime_error("Scene通知失敗の検証");
						}
					}
				},
				&state);
			bool rejected = false;
			try {
				switch (mode) {
				case 0:
					// Worldの先行破棄後に生成を確定しない
					owned.reset();
					scope->Commit();
					break;
				case 1:
					// 取消通知で終了したWorldの階層を再構築しない
					scope.reset();
					rejected = !lifetime->IsAlive();
					break;
				case 2:
					SceneInstantiator::LoadFromJson(scene, *owned, &database, sceneID, Engine::UUID::New(), nullptr);
					break;
				case 3:
					system.LoadScene(scenePath, *owned, &database, sceneID, Engine::UUID::New());
					break;
				case 4:
					scenes.LoadAdditive(database, system, *owned, sceneID);
					break;
				case 5:
					scenes.LoadSceneTree(database, system, *owned, sceneID);
					break;
				case 6: {
					owned->SetCommandServices({&database, &scenes, &system});
					owned->GetCommandBuffer().EnqueueLoadSceneSingle(Engine::UUID::New(), sceneID);
					owned->FlushWorldCommands();
					break;
				}
				case 7: {
					SceneHeader parent;
					parent.subScenes.push_back({Engine::UUID::New(), "Child", sceneID, true});
					const Engine::UUID instanceID = scenes.CreateScratchScene(parent);
					scenes.SynchronizeSubScenes(database, system, *owned, instanceID);
					break;
				}
				case 8: {
					PrefabInstantiateResult result;
					PrefabInstantiator::InstantiatePrefab(database, hierarchy, *owned, prefabID, result, {});
					break;
				}
				case 9: {
					PrefabInstanceData data;
					data.prefabAsset = prefabID;
					data.instanceID = Engine::UUID::New();
					for (const auto& entity : entities) {
						data.entityMap.emplace_back(
							FromString16Hex(entity["LocalFileID"].get<std::string>()), Engine::UUID::New());
					}
					PrefabInstanceRebuilder::RebuildInstance(*owned, database, hierarchy, data, Engine::UUID::New(), 0);
					break;
				}
				}
			} catch (const std::runtime_error& error) {
				rejected = mode == 0 || !failAfterEnding || std::string(error.what()) == "Scene通知失敗の検証";
			}
			if (!rejected || owned || lifetime->IsAlive() || (mode > 1 && state.calls != state.threshold)) {
				std::cerr << "Scene creation lifetime failed: mode=" << mode << " fail=" << failAfterEnding << '\n';
				return false;
			}
		}
	}
	return CheckCommandConnection(database, system, sceneID);
}
