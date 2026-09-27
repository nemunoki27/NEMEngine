#include "EditorDeleteCommandTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/DeleteEntityCommand.h>
#include <Engine/Editor/Commands/Core/CompositeEditorCommand.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>

namespace {

	std::unique_ptr<Engine::CompositeEditorCommand> MakeDelete(Engine::ECSWorld& world, Engine::EditorState& state) {

		std::vector<std::unique_ptr<Engine::IEditorCommand>> commands;
		for (const auto& entity : Engine::HierarchyUtility::CollectLogicalRoots(world, state.GetSelectedEntities())) {
			commands.emplace_back(std::make_unique<Engine::DeleteEntityCommand>(entity));
		}
		return std::make_unique<Engine::CompositeEditorCommand>(std::move(commands));
	}
}

bool NEMTests::TestBulkDeleteRecovery() {

	using namespace Engine;
	ECSWorld world;
	EditorState state;
	EditorContext editor;
	editor.activeWorld = &world;
	EditorCommandContext context;
	context.editorContext = &editor;
	context.editorState = &state;
	const Entity root = SceneAuthoring::CreateGameObject(world, "Root");
	const Entity child = SceneAuthoring::CreateGameObject(world, "Child");
	const Entity other = SceneAuthoring::CreateGameObject(world, "Other");
	const auto rootID = world.GetUUID(root), childID = world.GetUUID(child), otherID = world.GetUUID(other);
	HierarchySystem hierarchy;
	hierarchy.SetParent(world, child, root);
	state.SetSelectedEntities({ child, root, other });
	state.selectedEntity = child;

	// 二つ目を削除禁止にして一つ目の階層を取り消す
	world.AddComponent<PrefabLinkComponent>(other).isPrefabRoot = false;
	if (state.commandHistory.Execute(MakeDelete(world, state), context) || state.commandHistory.CanUndo()) {
		return false;
	}
	Entity restoredRoot = world.FindByUUID(rootID);
	Entity restoredChild = world.FindByUUID(childID);
	if (!world.IsAlive(restoredRoot) || !world.IsAlive(restoredChild) || !world.IsAlive(other) ||
		state.selectedEntity != restoredChild ||
		state.selectedEntities != std::vector<Entity>{ restoredChild, restoredRoot, other } ||
		world.GetComponent<HierarchyComponent>(restoredChild).parent != restoredRoot) {
		return false;
	}
	world.RemoveComponent<PrefabLinkComponent>(other);
	if (!state.commandHistory.Execute(MakeDelete(world, state), context) ||
		state.commandHistory.GetUndoCount() != 1 || world.IsAlive(restoredRoot) || world.IsAlive(restoredChild) ||
		world.IsAlive(other)) {
		return false;
	}

	// 一度のUndoで全階層と選択を復元する
	if (!state.commandHistory.Undo(context)) {
		return false;
	}
	restoredRoot = world.FindByUUID(rootID);
	restoredChild = world.FindByUUID(childID);
	const Entity restoredOther = world.FindByUUID(otherID);
	if (!world.IsAlive(restoredRoot) || !world.IsAlive(restoredChild) || !world.IsAlive(restoredOther) ||
		state.selectedEntity != restoredChild ||
		state.selectedEntities != std::vector<Entity>{ restoredChild, restoredRoot, restoredOther } ||
		world.GetComponent<HierarchyComponent>(restoredChild).parent != restoredRoot) {
		return false;
	}
	return state.commandHistory.Redo(context) && state.commandHistory.GetUndoCount() == 1 &&
		!world.IsAlive(restoredRoot) && !world.IsAlive(restoredChild) && !world.IsAlive(restoredOther);
}
