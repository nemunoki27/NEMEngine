#include "CloneEntityTreesCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/DeleteEntityCommand.h>
#include <Engine/Editor/Commands/Entity/EditorEntityDuplicateUtility.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/World/Scene/Serialization/EntitySnapshotBatchDuplicator.h>
#include <Engine/Core/World/Scene/Serialization/SceneCreationScope.h>
#include <Engine/Core/World/Scene/Serialization/EntitySnapshotPlacement.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>

// c++
#include <stdexcept>

//============================================================================
//	CloneEntityTreesCommand classMethods
//============================================================================
Engine::CloneEntityTreesCommand::CloneEntityTreesCommand(std::vector<Entity> targets) :
	targets_(std::move(targets)), retainSourceScene_(true) {
}

Engine::CloneEntityTreesCommand::CloneEntityTreesCommand(std::vector<EditorEntityTreeSnapshot> sources,
	std::vector<UUID> parents, bool retainSourceScene) :
	sources_(std::move(sources)), parents_(std::move(parents)), retainSourceScene_(retainSourceScene) {
}

void Engine::CloneEntityTreesCommand::PrepareDestinations(EditorCommandContext& context,
	std::vector<EditorEntityTreeSnapshot>& snapshots) {

	ECSWorld& world = *context.GetWorld();
	const EditorContext* editor = context.editorContext;
	const SceneInstanceManager* scenes = editor ? editor->sceneInstances : nullptr;
	std::vector<EntitySnapshotScene> destinations;
	for (size_t index = 0; index < snapshots.size(); ++index) {
		const auto& source = snapshots[index];
		EntitySnapshotScene destination{ source.ownerSceneInstanceID, source.ownerSourceAsset };
		const bool sourceLoaded = retainSourceScene_ && (!scenes || scenes->Find(source.ownerSceneInstanceID));
		if (!sourceLoaded) {
			// 別Worldやアンロード済みSceneの親へ接続しない
			parents_[index] = {};
			destination = { editor ? editor->activeSceneInstanceID : UUID{}, editor ? editor->activeSceneAsset : AssetID{} };
		}
		if (const SceneInstance* scene = scenes ? scenes->Find(destination.instanceID) : nullptr) {
			destination.asset = scene->sceneAsset;
		}
		if (parents_[index]) {
			const Entity parent = world.FindByUUID(parents_[index]);
			const auto* membership = world.TryGetComponent<SceneObjectComponent>(parent);
			if (!membership || membership->sceneInstanceID != destination.instanceID) {
				parents_[index] = {};
			}
		}
		destinations.emplace_back(destination);
	}
	EntitySnapshotPlacement::Apply(snapshots, destinations);
}

bool Engine::CloneEntityTreesCommand::CaptureSources(ECSWorld& world) {

	if (targets_.empty()) {
		return !sources_.empty() && sources_.size() == parents_.size();
	}
	std::vector<EditorEntityTreeSnapshot> sources;
	std::vector<UUID> parents;
	for (const Entity& target : targets_) {
		if (!world.IsAlive(target)) {
			return false;
		}
		EditorEntityTreeSnapshot snapshot;
		EditorEntitySnapshotUtility::CaptureSubtree(world, target, snapshot);
		if (snapshot.IsEmpty()) {
			return false;
		}
		const auto* hierarchy = world.TryGetComponent<HierarchyComponent>(target);
		parents.emplace_back(hierarchy && world.IsAlive(hierarchy->parent) ? world.GetUUID(hierarchy->parent) : UUID{});
		sources.emplace_back(std::move(snapshot));
	}
	sources_ = std::move(sources);
	parents_ = std::move(parents);
	return true;
}

bool Engine::CloneEntityTreesCommand::Execute(EditorCommandContext& context) {

	if (!context.CanEditScene() || !context.GetWorld() || !createdRoots_.empty()) {
		return false;
	}
	ECSWorld& world = *context.GetWorld();
	if (!CaptureSources(world)) {
		return false;
	}
	previousSelection_.Capture(context);
	auto prepared = EntitySnapshotBatchDuplicator::Build(sources_);
	PrepareDestinations(context, prepared);
	SceneCreationScope creation(world);
	std::vector<Entity> roots;
	std::vector<UUID> createdRoots;
	try {
		for (size_t index = 0; index < prepared.size(); ++index) {
			auto& snapshot = prepared[index];
			for (auto& saved : snapshot.entities) {
				if (saved.stableUUID != snapshot.rootStableUUID) {
					continue;
				}
				// 直前に作った階層も含めて名前の重複を避ける
				const auto name = saved.components.find("Name");
				const std::string originalName = name == saved.components.end() ? "Entity" : name->value("name", "Entity");
				saved.components["Name"]["name"] = EditorEntityDuplicateUtility::MakeUniqueDuplicatedName(world, originalName);
				break;
			}
			EditorEntitySnapshotUtility::FillMissingOwnerRuntimeState(context, world, parents_[index], snapshot);
			const Entity root = EditorEntityDuplicateUtility::InstantiatePreparedSnapshot(world, snapshot, parents_[index]);
			if (!world.IsAlive(root)) {
				throw std::runtime_error("複製した階層のルートを取得できません");
			}
			const auto restored = EditorEntitySnapshotUtility::CollectSubtreeEntities(world, root);
			EditorEntitySnapshotUtility::RefreshRestoredRuntimeState(context, world, snapshot, restored);
			roots.emplace_back(root);
			createdRoots.emplace_back(snapshot.rootStableUUID);
		}
		// 全対象が揃ってから選択と履歴を確定する
		context.RebuildHierarchyAll();
		if (context.editorState) {
			context.editorState->SetSelectedEntities(roots);
		}
		appliedSelection_.Capture(context);
		createdRoots_ = std::move(createdRoots);
		creation.Commit();
		// 以後のUndoは生成物を保存するため元階層を保持しない
		sources_.clear();
		parents_.clear();
		targets_.clear();
		return true;
	} catch (...) {
		previousSelection_.Restore(context);
		throw;
	}
}

void Engine::CloneEntityTreesCommand::Undo(EditorCommandContext& context) {

	if (!context.CanEditScene() || !context.GetWorld() || createdRoots_.empty()) {
		throw std::runtime_error("複製を取り消せるWorldがありません");
	}
	std::vector<std::unique_ptr<IEditorCommand>> commands;
	for (const UUID id : createdRoots_) {
		const Entity root = context.GetWorld()->FindByUUID(id);
		if (!context.GetWorld()->IsAlive(root)) {
			throw std::runtime_error("複製した階層が失われています");
		}
		commands.emplace_back(std::make_unique<DeleteEntityCommand>(root));
	}
	// 削除途中の失敗も一括操作の復元処理へ渡す
	auto removal = std::make_unique<CompositeEditorCommand>(std::move(commands));
	if (!removal->Execute(context)) {
		throw std::runtime_error("複製した階層を削除できません");
	}
	previousSelection_.Restore(context);
	removal_ = std::move(removal);
}

bool Engine::CloneEntityTreesCommand::Redo(EditorCommandContext& context) {

	if (!context.CanEditScene() || !context.GetWorld() || !removal_) {
		return false;
	}
	// 再採番せず、取消時に保持した全階層を戻す
	removal_->Undo(context);
	appliedSelection_.Restore(context);
	removal_.reset();
	return true;
}
