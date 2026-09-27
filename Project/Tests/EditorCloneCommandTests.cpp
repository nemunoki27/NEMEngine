#include "EditorCloneCommandTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/CloneEntityTreesCommand.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Components/Camera/CameraControllerComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>

// c++
#include <stdexcept>

namespace {

	bool TestPasteDestination() {

		using namespace Engine;
		ECSWorld sourceWorld;
		const Entity source = SceneAuthoring::CreateGameObject(sourceWorld, "Source");
		const auto parentID = Engine::UUID::New();
		auto& sourceMembership = sourceWorld.GetComponent<SceneObjectComponent>(source);
		sourceMembership.sceneInstanceID = Engine::UUID{ 10 };
		sourceMembership.sourceAsset = AssetID{ 1, 2 };
		EditorEntityTreeSnapshot snapshot;
		EditorEntitySnapshotUtility::CaptureSubtree(sourceWorld, source, snapshot);

		ECSWorld destination;
		const Entity unrelated = destination.CreateEntity(parentID);
		SceneAuthoring::EnsureGameObjectDefaults(destination, unrelated);
		EditorState state;
		EditorContext editor;
		editor.activeWorld = &destination;
		editor.activeSceneInstanceID = Engine::UUID{ 20 };
		editor.activeSceneAsset = AssetID{ 3, 4 };
		EditorCommandContext context;
		context.editorContext = &editor;
		context.editorState = &state;
		state.SelectEntity(unrelated);
		if (!state.commandHistory.Execute(std::make_unique<CloneEntityTreesCommand>(
			std::vector<EditorEntityTreeSnapshot>{ snapshot }, std::vector<Engine::UUID>{ parentID }), context)) {
			return false;
		}
		const Entity pasted = state.selectedEntity;
		const auto pastedID = destination.GetUUID(pasted);
		const auto& membership = destination.GetComponent<SceneObjectComponent>(pasted);
		if (membership.sceneInstanceID != editor.activeSceneInstanceID || membership.sourceAsset != editor.activeSceneAsset ||
			destination.GetComponent<HierarchyComponent>(pasted).parent.IsValid()) {
			return false;
		}
		if (!state.commandHistory.Undo(context)) {
			return false;
		}
		editor.activeSceneInstanceID = Engine::UUID{ 30 };
		editor.activeSceneAsset = AssetID{ 5, 6 };
		if (!state.commandHistory.Redo(context)) {
			return false;
		}
		const auto& restored = destination.GetComponent<SceneObjectComponent>(destination.FindByUUID(pastedID));
		return restored.sceneInstanceID == Engine::UUID{ 20 } && restored.sourceAsset == AssetID{ 3, 4 };
	}
}

bool NEMTests::TestBulkCloneRecovery() {

	if (!TestPasteDestination()) {
		return false;
	}
	using namespace Engine;
	ECSWorld world;
	EditorState state;
	EditorContext editor;
	editor.activeWorld = &world;
	EditorCommandContext context;
	context.editorContext = &editor;
	context.editorState = &state;
	const Entity first = SceneAuthoring::CreateGameObject(world, "CloneSource");
	const Entity second = SceneAuthoring::CreateGameObject(world, "CloneSource");
	const auto targetID = world.GetComponent<SceneObjectComponent>(second).localFileID;
	world.AddComponent<CameraControllerComponent>(first).follow.target = targetID;
	state.SetSelectedEntities({ first, second });
	if (!state.commandHistory.Execute(std::make_unique<CloneEntityTreesCommand>(std::vector<Entity>{ first, second }), context) ||
		state.selectedEntities.size() != 2 || state.commandHistory.GetUndoCount() != 1) {
		return false;
	}
	const Entity cloneFirst = state.selectedEntities[0], cloneSecond = state.selectedEntities[1];
	const auto firstID = world.GetUUID(cloneFirst), secondID = world.GetUUID(cloneSecond);
	const auto cloneLocalID = world.GetComponent<SceneObjectComponent>(cloneSecond).localFileID;
	if (world.GetComponent<CameraControllerComponent>(cloneFirst).follow.target != cloneLocalID ||
		world.GetComponent<NameComponent>(cloneFirst).name == world.GetComponent<NameComponent>(cloneSecond).name) {
		return false;
	}
	if (!state.commandHistory.Undo(context) || world.IsAlive(cloneFirst) || world.IsAlive(cloneSecond) ||
		state.selectedEntities != std::vector<Entity>{ first, second }) {
		return false;
	}
	if (!state.commandHistory.Redo(context)) {
		return false;
	}
	const Entity restoredFirst = world.FindByUUID(firstID), restoredSecond = world.FindByUUID(secondID);
	if (state.selectedEntities != std::vector<Entity>{ restoredFirst, restoredSecond } ||
		world.GetComponent<CameraControllerComponent>(restoredFirst).follow.target != cloneLocalID ||
		world.GetComponent<SceneObjectComponent>(restoredSecond).localFileID != cloneLocalID) {
		return false;
	}

	// 二つ目の復元失敗で一つ目の生成物と選択を戻す
	std::vector<EditorEntityTreeSnapshot> broken(2);
	EditorEntitySnapshotUtility::CaptureSubtree(world, first, broken[0]);
	EditorEntitySnapshotUtility::CaptureSubtree(world, second, broken[1]);
	broken[1].entities[0].components["Name"]["name"] = 123;
	const auto selection = state.selectedEntities;
	size_t before = 0;
	world.ForEachAliveEntity([&](Entity) { ++before; });
	bool rejected = false;
	try {
		state.commandHistory.Execute(std::make_unique<CloneEntityTreesCommand>(broken, std::vector<Engine::UUID>(2)), context);
	} catch (const nlohmann::json::exception&) {
		rejected = true;
	}
	size_t after = 0;
	world.ForEachAliveEntity([&](Entity) { ++after; });
	return rejected && before == after && state.selectedEntities == selection && state.commandHistory.GetUndoCount() == 1;
}
