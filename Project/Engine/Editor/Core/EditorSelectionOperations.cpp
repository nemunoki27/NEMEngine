#include "EditorSelectionOperations.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/EditorEntityDuplicateUtility.h>
#include <Engine/Editor/Commands/Entity/DuplicateEntityCommand.h>
#include <Engine/Editor/Commands/Entity/PasteEntityTreeCommand.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>

bool Engine::EditorSelectionOperations::Duplicate(const EditorContext* context, EditorState& state, IEditorPanelHost& host) {

	if (!context) {
		return false;
	}
	if (!state.HasValidSelection(context->activeWorld) || context->isPlaying) {
		return false;
	}
	// 複数選択を順に複製する、選択や生存が変わるため対象を先にコピーしておく
	ECSWorld* world = context->activeWorld;
	const std::vector<Entity> targets = state.GetSelectedEntities();
	std::vector<Entity> duplicated;
	for (const Entity& target : targets) {
		if (world && world->IsAlive(target)) {
			// 各コマンドは複製ルートをselectedEntityへ入れるので実行後に集約する
			if (host.ExecuteEditorCommand(std::make_unique<DuplicateEntityCommand>(target))) {
				duplicated.push_back(state.selectedEntity);
			}
		}
	}
	if (duplicated.empty()) {
		return false;
	}
	// 複製した分をまとめて選択し直す
	state.SetSelectedEntities(duplicated);
	return true;
}

bool Engine::EditorSelectionOperations::Copy(const EditorContext& context, EditorState& state) {

	if (!state.HasValidSelection(context.activeWorld) || context.isPlaying) {
		return false;
	}

	ECSWorld& world = *context.activeWorld;

	// 複数選択をそれぞれ独立スナップショットとしてクリップボードへ保存する
	state.clipboardSnapshots.clear();
	state.clipboardParentUUIDs.clear();
	const std::vector<Entity> targets = state.GetSelectedEntities();
	for (const Entity& selected : targets) {

		if (!world.IsAlive(selected)) {
			continue;
		}
		EditorEntityTreeSnapshot snapshot{};
		EditorEntitySnapshotUtility::CaptureSubtree(world, selected, snapshot);
		if (snapshot.IsEmpty()) {
			continue;
		}

		// 各エンティティの親UUIDも控えておき、貼り付けは元の親付近へ行う
		UUID parentUUID{};
		if (world.HasComponent<HierarchyComponent>(selected)) {

			const auto& hierarchy = world.GetComponent<HierarchyComponent>(selected);
			if (world.IsAlive(hierarchy.parent)) {
				parentUUID = world.GetUUID(hierarchy.parent);
			}
		}
		// クリップボードは外部親を持たない独立スナップショットにしておく
		EditorEntityDuplicateUtility::ClearRootParentLink(snapshot);
		state.clipboardSnapshots.emplace_back(std::move(snapshot));
		state.clipboardParentUUIDs.emplace_back(parentUUID);
	}
	return !state.clipboardSnapshots.empty();
}

bool Engine::EditorSelectionOperations::Paste(const EditorContext* context, EditorState& state, IEditorPanelHost& host) {

	if (!context) {
		return false;
	}
	if (context->isPlaying || !state.HasClipboard()) {
		return false;
	}
	// クリップボードの各スナップショットを順に貼り付け、貼り付け先をまとめて選択する
	std::vector<Entity> pasted;
	for (size_t i = 0; i < state.clipboardSnapshots.size(); ++i) {

		const UUID parentUUID = i < state.clipboardParentUUIDs.size() ?
			state.clipboardParentUUIDs[i] : UUID{};
		if (host.ExecuteEditorCommand(std::make_unique<PasteEntityTreeCommand>(
			state.clipboardSnapshots[i], parentUUID))) {
			pasted.push_back(state.selectedEntity);
		}
	}
	if (pasted.empty()) {
		return false;
	}
	state.SetSelectedEntities(pasted);
	return true;
}
