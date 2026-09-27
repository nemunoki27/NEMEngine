#include "EditorSelectionOperations.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/EditorEntityDuplicateUtility.h>
#include <Engine/Editor/Commands/Entity/CloneEntityTreesCommand.h>
#include <Engine/Editor/Commands/Entity/DeleteEntityCommand.h>
#include <Engine/Editor/Commands/Core/CompositeEditorCommand.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Editor/Utility/PrefabInstanceEditUtility.h>

// c++
#include <memory>
#include <utility>

bool Engine::EditorSelectionOperations::Duplicate(const EditorContext* context, EditorState& state, IEditorPanelHost& host) {

	if (!context || !context->activeWorld) {
		return false;
	}
	if (!state.HasValidSelection(context->activeWorld) || context->isPlaying) {
		return false;
	}
	// 複製範囲を先に確定し、参照と選択を履歴内で更新する
	auto targets = HierarchyUtility::CollectLogicalRoots(*context->activeWorld, state.GetSelectedEntities());
	return !targets.empty() && host.ExecuteEditorCommand(std::make_unique<CloneEntityTreesCommand>(std::move(targets)));
}

bool Engine::EditorSelectionOperations::Delete(const EditorContext* context, EditorState& state, IEditorPanelHost& host) {

	if (!context || !context->activeWorld || context->isPlaying) {
		return false;
	}
	ECSWorld& world = *context->activeWorld;
	const std::vector<Entity> targets = HierarchyUtility::CollectLogicalRoots(world, state.GetSelectedEntities());
	std::vector<std::unique_ptr<IEditorCommand>> commands;
	for (const Entity& target : targets) {
		// 編集中Prefabのルートを含む場合は全体を変更前に止める
		if (!PrefabInstanceEditUtility::CanDelete(context, world, target)) {
			return false;
		}
		commands.emplace_back(std::make_unique<DeleteEntityCommand>(target));
	}
	return !commands.empty() && host.ExecuteEditorCommand(std::make_unique<CompositeEditorCommand>(std::move(commands)));
}

bool Engine::EditorSelectionOperations::Copy(const EditorContext& context, EditorState& state) {

	if (!context.activeWorld || !state.HasValidSelection(context.activeWorld) || context.isPlaying) {
		return false;
	}

	ECSWorld& world = *context.activeWorld;

	// 複数選択をそれぞれ独立スナップショットとしてクリップボードへ保存する
	std::vector<EditorEntityTreeSnapshot> snapshots;
	std::vector<UUID> parents;
	const std::vector<Entity> targets = HierarchyUtility::CollectLogicalRoots(world, state.GetSelectedEntities());
	for (const Entity& selected : targets) {

		if (!world.IsAlive(selected)) {
			return false;
		}
		EditorEntityTreeSnapshot snapshot{};
		EditorEntitySnapshotUtility::CaptureSubtree(world, selected, snapshot);
		if (snapshot.IsEmpty()) {
			return false;
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
		snapshots.emplace_back(std::move(snapshot));
		parents.emplace_back(parentUUID);
	}
	if (snapshots.empty()) {
		return false;
	}
	// 全対象の取得に成功してからクリップボードを差し替える
	state.clipboardSnapshots = std::move(snapshots);
	state.clipboardParentUUIDs = std::move(parents);
	state.clipboardWorld = world.GetLifetime();
	return true;
}

bool Engine::EditorSelectionOperations::Paste(const EditorContext* context, EditorState& state, IEditorPanelHost& host) {

	if (!context || !context->activeWorld || context->isPlaying || !state.HasClipboard()) {
		return false;
	}
	return host.ExecuteEditorCommand(std::make_unique<CloneEntityTreesCommand>(
		state.clipboardSnapshots, state.clipboardParentUUIDs,
		state.clipboardWorld.lock() == context->activeWorld->GetLifetime()));
}
