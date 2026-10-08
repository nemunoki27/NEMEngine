#include "EditorSelectionSnapshot.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/EditorCommandContext.h>

// c++
#include <algorithm>

void Engine::EditorSelectionSnapshot::Capture(const EditorCommandContext& context) {

	EditorSelectionSnapshot candidate;
	if (context.editorState) {
		const auto& state = *context.editorState;
		ECSWorld* world = context.GetWorld();
		candidate.worldBound_ = world != nullptr;
		if (world) {
			candidate.worldLifetime_ = world->GetLifetime();
		}
		candidate.kind_ = state.selectionKind;
		candidate.active_ = CaptureEntity(world, state.selectedEntity);
		candidate.joint_ = CaptureEntity(world, state.selectedJointSkinnedEntity);
		candidate.asset_ = state.selectedAsset;
		candidate.subMeshID_ = state.selectedSubMeshStableID;
		candidate.subMeshIndex_ = state.selectedSubMeshIndex;
		candidate.jointIndex_ = state.selectedJointIndex;
		candidate.selected_.reserve(state.selectedEntities.size());
		for (const Entity& entity : state.selectedEntities) {
			candidate.selected_.emplace_back(CaptureEntity(world, entity));
		}
	}
	*this = std::move(candidate);
}

bool Engine::EditorSelectionSnapshot::Restore(const EditorCommandContext& context) const {

	if (!context.editorState) {
		return false;
	}
	ECSWorld* world = context.GetWorld();
	// World切替後へ古い選択を持ち越さない
	if (worldBound_ && (!world || worldLifetime_.lock() != world->GetLifetime())) {
		return false;
	}
	auto& state = *context.editorState;
	const Entity active = ResolveEntity(world, active_);
	switch (kind_) {
	case EditorSelectionKind::Entity: {
		std::vector<Entity> entities;
		entities.reserve(selected_.size());
		for (const auto& saved : selected_) {
			const Entity entity = ResolveEntity(world, saved);
			if (entity.IsValid()) {
				entities.emplace_back(entity);
			}
		}
		state.SetSelectedEntities(entities);
		if (std::find(entities.begin(), entities.end(), active) != entities.end()) {
			state.selectedEntity = active;
		}
		break;
	}
	case EditorSelectionKind::MeshSubMesh:
		state.SelectMeshSubMesh(active, subMeshIndex_, subMeshID_);
		break;
	case EditorSelectionKind::Joint:
		state.SelectJoint(ResolveEntity(world, joint_), jointIndex_);
		break;
	case EditorSelectionKind::Asset:
		state.SelectAsset(asset_);
		break;
	case EditorSelectionKind::None:
	default:
		state.ClearSelection();
		break;
	}
	return true;
}

Engine::EditorSelectionSnapshot::SelectionEntity Engine::EditorSelectionSnapshot::CaptureEntity(
	ECSWorld* world, const Entity& entity) {

	return { entity, world && world->IsAlive(entity) ? world->GetUUID(entity) : UUID{} };
}

Engine::Entity Engine::EditorSelectionSnapshot::ResolveEntity(ECSWorld* world, const SelectionEntity& entity) {

	if (!world) {
		return entity.handle;
	}
	// Undoでhandleが変わっても同じ保存IDの実体へ戻す
	return entity.stableUUID ? world->FindByUUID(entity.stableUUID) : Entity::Null();
}
