#include "HierarchyCommandContractTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/ReparentEntitiesCommand.h>
#include <Engine/Editor/Commands/Entity/ReorderEntityCommand.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>

namespace {

	using namespace Engine;

	// 再実行とUndoで初回の親を維持する
	bool TestReparentRollbackAndRetry() {

		ECSWorld world;
		EditorContext editor;
		editor.activeWorld = &world;
		EditorState state;
		EditorCommandContext context;
		context.editorContext = &editor;
		context.editorState = &state;
		const Entity oldParent = SceneAuthoring::CreateGameObject(world, "Old");
		const Entity first = SceneAuthoring::CreateGameObject(world, "First");
		const Entity second = SceneAuthoring::CreateGameObject(world, "Second");
		const Entity destination = SceneAuthoring::CreateGameObject(world, "Destination");
		HierarchySystem hierarchy;
		hierarchy.SetParent(world, first, oldParent);
		hierarchy.SetParent(world, destination, second);
		ReparentEntitiesCommand command({first, second}, world.GetUUID(destination));
		// 2件目の循環で1件目を元の親へ戻す
		if (command.Execute(context) || HierarchyUtility::GetParent(world, first) != oldParent ||
			HierarchyUtility::GetParent(world, destination) != second || HierarchyUtility::GetParent(world, second).IsValid()) {
			return false;
		}
		hierarchy.SetParent(world, destination, Entity::Null());
		if (!command.Execute(context) || HierarchyUtility::GetParent(world, first) != destination ||
			HierarchyUtility::GetParent(world, second) != destination) {
			return false;
		}
		command.Undo(context);
		if (HierarchyUtility::GetParent(world, first) != oldParent || HierarchyUtility::GetParent(world, second).IsValid()) {
			return false;
		}
		return command.Redo(context) && HierarchyUtility::GetParent(world, first) == destination &&
			   HierarchyUtility::GetParent(world, second) == destination;
	}

	// 別Sceneへの親変更で適用済みの変更も取り消す
	bool TestReparentSceneBoundary() {

		ECSWorld world;
		EditorContext editor;
		editor.activeWorld = &world;
		EditorCommandContext context;
		context.editorContext = &editor;
		const Entity destination = SceneAuthoring::CreateGameObject(world, "Destination");
		const Entity first = SceneAuthoring::CreateGameObject(world, "First");
		const Entity second = SceneAuthoring::CreateGameObject(world, "Second");
		world.GetComponent<SceneObjectComponent>(destination).sceneInstanceID = Engine::UUID{1};
		world.GetComponent<SceneObjectComponent>(first).sceneInstanceID = Engine::UUID{1};
		world.GetComponent<SceneObjectComponent>(second).sceneInstanceID = Engine::UUID{2};
		ReparentEntitiesCommand command({first, second}, world.GetUUID(destination));
		return !command.Execute(context) && !HierarchyUtility::GetParent(world, first).IsValid() &&
			   !HierarchyUtility::GetParent(world, second).IsValid() &&
			   world.GetComponent<SceneObjectComponent>(second).sceneInstanceID == Engine::UUID{2};
	}

	// Root並替えで別Sceneの順序を変更しない
	bool TestRootReorderSceneBoundary() {

		ECSWorld world;
		EditorContext editor;
		editor.activeWorld = &world;
		EditorCommandContext context;
		context.editorContext = &editor;
		const Entity first = SceneAuthoring::CreateGameObject(world, "First");
		const Entity second = SceneAuthoring::CreateGameObject(world, "Second");
		const Entity other = SceneAuthoring::CreateGameObject(world, "Other");
		world.GetComponent<SceneObjectComponent>(first).sceneInstanceID = Engine::UUID{1};
		world.GetComponent<SceneObjectComponent>(second).sceneInstanceID = Engine::UUID{1};
		world.GetComponent<SceneObjectComponent>(other).sceneInstanceID = Engine::UUID{2};
		world.GetComponent<HierarchyComponent>(first).siblingOrder = 0;
		world.GetComponent<HierarchyComponent>(second).siblingOrder = 1;
		world.GetComponent<HierarchyComponent>(other).siblingOrder = 50;
		ReorderEntityCommand command(first, second, true);
		if (!command.Execute(context) || world.GetComponent<HierarchyComponent>(first).siblingOrder != 1 ||
			world.GetComponent<HierarchyComponent>(second).siblingOrder != 0 ||
			world.GetComponent<HierarchyComponent>(other).siblingOrder != 50) {
			return false;
		}
		command.Undo(context);
		if (world.GetComponent<HierarchyComponent>(first).siblingOrder != 0 ||
			world.GetComponent<HierarchyComponent>(second).siblingOrder != 1 ||
			world.GetComponent<HierarchyComponent>(other).siblingOrder != 50) {
			return false;
		}
		return command.Redo(context) && world.GetComponent<HierarchyComponent>(first).siblingOrder == 1 &&
			   world.GetComponent<HierarchyComponent>(other).siblingOrder == 50;
	}
}

bool NEMTests::TestHierarchyCommandContracts() {

	return TestReparentRollbackAndRetry() && TestReparentSceneBoundary() && TestRootReorderSceneBoundary();
}
