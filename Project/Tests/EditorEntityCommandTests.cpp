#include "EditorEntityCommandTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/CreateEntityCommand.h>
#include <Engine/Editor/Commands/Entity/InstantiatePrefabCommand.h>
#include <Engine/Editor/Commands/Core/EditorCommandExecution.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Core/EditorSceneDirtyState.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>

bool NEMTests::TestEditorEntityCommandRedo() {

	using namespace Engine;
	ECSWorld world;
	EditorState state;
	SceneInstanceManager scenes;
	const auto originalScene = scenes.CreateScratchScene({});
	const auto otherScene = scenes.CreateScratchScene({});
	scenes.Find(originalScene)->sceneAsset = AssetID{ 82, 83 };
	scenes.Find(otherScene)->sceneAsset = AssetID{ 85, 86 };
	EditorContext editor;
	editor.activeWorld = &world;
	editor.sceneInstances = &scenes;
	editor.activeSceneInstanceID = originalScene;
	editor.activeSceneAsset = AssetID{ 82, 83 };
	EditorCommandContext context;
	context.editorContext = &editor;
	context.editorState = &state;
	CreateEntityCommand create("Created");
	if (!create.Execute(context)) {
		return false;
	}
	const Entity original = state.selectedEntity;
	const auto stableID = world.GetUUID(original);
	const auto membership = world.GetComponent<SceneObjectComponent>(original);
	create.Undo(context);
	editor.activeSceneInstanceID = otherScene;
	editor.activeSceneAsset = AssetID{ 85, 86 };
	EditorSceneDirtyState dirty;
	if (!EditorCommandExecution::Run(editor, state, dirty,
		[&](EditorCommandContext& current) { return create.Redo(current); })) {
		return false;
	}
	const Entity restored = world.FindByUUID(stableID);
	const auto& restoredMembership = world.GetComponent<SceneObjectComponent>(restored);
	// Active Sceneが変わっても初回のIDと所属へ戻す
	if (restored == original || state.selectedEntity != restored ||
		restoredMembership.localFileID != membership.localFileID ||
		restoredMembership.sceneInstanceID != membership.sceneInstanceID ||
		restoredMembership.sourceAsset != membership.sourceAsset ||
		!dirty.IsSceneDirty(membership.sourceAsset, originalScene) ||
		dirty.IsSceneDirty(editor.activeSceneAsset, otherScene)) {
		return false;
	}

	TestDirectory directory("EditorEntityCommand", RuntimePaths::GetGameAssetsRoot());
	const auto path = directory.GetPath() / "Source.prefab.json";
	nlohmann::json prefab = {
		{ "SchemaVersion", 2 }, { "Header", { { "rootLocalFileID", "0000000000000001" } } },
		{ "Entities", nlohmann::json::array({ {
			{ "LocalFileID", "0000000000000001" },
			{ "Components", { { "Name", { { "name", "Initial" } } } } }
		} }) }
	};
	if (!JsonFile::Save(path, prefab)) {
		return false;
	}
	AssetDatabase database;
	database.Init();
	editor.assetDatabase = &database;
	const auto asset = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Prefab);
	InstantiatePrefabCommand instantiate(asset);
	if (!asset || !instantiate.Execute(context)) {
		return false;
	}
	const Entity prefabRoot = state.selectedEntity;
	const auto prefabStableID = world.GetUUID(prefabRoot);
	const auto instanceID = world.GetComponent<PrefabLinkComponent>(prefabRoot).prefabInstanceID;
	const auto localID = world.GetComponent<SceneObjectComponent>(prefabRoot).localFileID;
	const auto initialName = world.GetComponent<NameComponent>(prefabRoot).name;
	instantiate.Undo(context);
	// Undo中のAsset変更をRedoへ混ぜない
	prefab["Entities"][0]["Components"]["Name"]["name"] = 123;
	if (!JsonFile::Save(path, prefab) || !instantiate.Redo(context)) {
		return false;
	}
	const Entity restoredPrefab = world.FindByUUID(prefabStableID);
	return world.IsAlive(restoredPrefab) && state.selectedEntity == restoredPrefab &&
		world.GetComponent<PrefabLinkComponent>(restoredPrefab).prefabInstanceID == instanceID &&
		world.GetComponent<SceneObjectComponent>(restoredPrefab).localFileID == localID &&
		world.GetComponent<NameComponent>(restoredPrefab).name == initialName;
}

bool NEMTests::TestLogicalSelectionRoots() {

	using namespace Engine;
	ECSWorld world;
	const Entity root = SceneAuthoring::CreateGameObject(world, "Root");
	const Entity child = SceneAuthoring::CreateGameObject(world, "Child");
	const Entity grandchild = SceneAuthoring::CreateGameObject(world, "Grandchild");
	const Entity joint = SceneAuthoring::CreateGameObject(world, "Joint");
	const Entity other = SceneAuthoring::CreateGameObject(world, "Other");
	const auto rootLocalID = world.GetComponent<SceneObjectComponent>(root).localFileID;
	for (const Entity entity : { root, child, grandchild, joint }) {
		world.GetComponent<SceneObjectComponent>(entity).sceneInstanceID = Engine::UUID{ 1 };
	}
	world.GetComponent<SceneObjectComponent>(other).sceneInstanceID = Engine::UUID{ 2 };
	world.GetComponent<SceneObjectComponent>(other).localFileID = rootLocalID;
	world.AddComponent<JointAttachmentComponent>(joint).skinnedEntityLocalFileID = rootLocalID;
	HierarchySystem hierarchy;
	hierarchy.SetParent(world, child, root);
	hierarchy.SetParent(world, grandchild, child);

	// 子を先に選択していても親の処理へまとめる
	std::vector<Entity> selection{ child, joint, root, root, other, Entity::Null() };
	if (HierarchyUtility::CollectLogicalRoots(world, selection) != std::vector<Entity>{ root, other }) {
		return false;
	}
	selection = { grandchild, child, other };
	if (HierarchyUtility::CollectLogicalRoots(world, selection) != std::vector<Entity>{ child, other }) {
		return false;
	}
	// 別Sceneの同じLocalFileIDをJointの親と見なさない
	selection = { joint, other };
	if (HierarchyUtility::CollectLogicalRoots(world, selection) != selection) {
		return false;
	}
	world.DestroyEntity(other);
	world.FlushPendingDestroyEntities();
	return HierarchyUtility::CollectLogicalRoots(world, selection) == std::vector<Entity>{ joint };
}
