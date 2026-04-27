#include "EditorState.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
#include <Engine/Core/Graphics/Mesh/MeshSubMeshAuthoring.h> 

//============================================================================
//	EditorState classMethods
//============================================================================

void Engine::EditorState::ValidateSelection(ECSWorld* world) {

	// アセット選択はECSWorldに依存しない
	if (selectionKind == EditorSelectionKind::Asset) {
		return;
	}

	// 選択しているエンティティがワールドに存在しない場合は選択をクリアする
	if (!world || !world->IsAlive(selectedEntity)) {

		ClearSelection();
		return;
	}

	// サブメッシュが選択されている場合は、選択が有効か確認する
	if (selectionKind == EditorSelectionKind::MeshSubMesh) {

		// MeshRendererを所持しているか
		if (!world->HasComponent<MeshRendererComponent>(selectedEntity)) {

			selectionKind = EditorSelectionKind::Entity;
			selectedSubMeshIndex = 0;
			selectedSubMeshStableID = UUID{};
			return;
		}
		uint32_t resolvedIndex = 0;
		if (!TryResolveSelectedSubMeshIndex(world, resolvedIndex)) {

			selectionKind = EditorSelectionKind::Entity;
			selectedSubMeshIndex = 0;
			selectedSubMeshStableID = UUID{};
			return;
		}
		// 現在の配列位置をキャッシュし直す
		selectedSubMeshIndex = resolvedIndex;
	}
}

void Engine::EditorState::SelectEntity(const Entity& entity) {

	selectionKind = entity.IsValid() ? EditorSelectionKind::Entity : EditorSelectionKind::None;
	selectedEntity = entity;
	selectedAsset = {};
	selectedSubMeshIndex = 0;
	selectedSubMeshStableID = UUID{};
}

void Engine::EditorState::SelectMeshSubMesh(const Entity& entity, uint32_t subMeshIndex, UUID stableID) {

	selectionKind = entity.IsValid() ? EditorSelectionKind::MeshSubMesh : EditorSelectionKind::None;
	selectedEntity = entity;
	selectedAsset = {};
	selectedSubMeshIndex = subMeshIndex;
	selectedSubMeshStableID = stableID;
}

void Engine::EditorState::SelectAsset(AssetID asset) {

	selectionKind = asset ? EditorSelectionKind::Asset : EditorSelectionKind::None;
	selectedAsset = asset;
	selectedEntity = Entity::Null();
	selectedSubMeshIndex = 0;
	selectedSubMeshStableID = UUID{};
}

void Engine::EditorState::SelectFromScenePick(const Entity& entity, uint32_t subMeshIndex, UUID stableID) {

	if (!entity.IsValid()) {
		ClearSelection();
		return;
	}

	// シーンピックからの選択は現在の選択モードに従う
	switch (selectKind) {
	case EditorSelectionKind::MeshSubMesh:

		SelectMeshSubMesh(entity, subMeshIndex, stableID);
		break;

	case EditorSelectionKind::Entity:

		SelectEntity(entity);
		break;
	}
}

bool Engine::EditorState::HasValidSubMeshSelection(ECSWorld* world) const {

	if (!world || selectionKind != EditorSelectionKind::MeshSubMesh || !world->IsAlive(selectedEntity)) {
		return false;
	}
	if (!world->HasComponent<MeshRendererComponent>(selectedEntity)) {
		return false;
	}

	const auto& meshRenderer = world->GetComponent<MeshRendererComponent>(selectedEntity);
	return selectedSubMeshIndex < meshRenderer.subMeshes.size();
}

bool Engine::EditorState::TryResolveSelectedSubMeshIndex(ECSWorld* world, uint32_t& outSubMeshIndex) const {

	if (!world || selectionKind != EditorSelectionKind::MeshSubMesh || !world->IsAlive(selectedEntity)) {
		return false;
	}
	if (!world->HasComponent<MeshRendererComponent>(selectedEntity)) {
		return false;
	}

	const auto& meshRenderer = world->GetComponent<MeshRendererComponent>(selectedEntity);
	// ID優先で解決
	if (selectedSubMeshStableID) {
		const int32_t found = MeshSubMeshAuthoring::FindSubMeshIndexByStableID(meshRenderer, selectedSubMeshStableID);
		if (0 <= found) {
			outSubMeshIndex = static_cast<uint32_t>(found);
			return true;
		}
	}
	// フォールバック
	if (selectedSubMeshIndex < meshRenderer.subMeshes.size()) {
		outSubMeshIndex = selectedSubMeshIndex;
		return true;
	}
	return false;
}

bool Engine::EditorState::IsEntitySelected(const Entity& entity) const {

	return selectionKind == EditorSelectionKind::Entity && selectedEntity == entity;
}

bool Engine::EditorState::IsMeshSubMeshSelected(const Entity& entity, UUID stableID, uint32_t subMeshIndex) const {

	if (selectionKind != EditorSelectionKind::MeshSubMesh || selectedEntity != entity) {
		return false;
	}
	if (stableID && selectedSubMeshStableID) {
		return stableID == selectedSubMeshStableID;
	}
	return selectedSubMeshIndex == subMeshIndex;
}

bool Engine::EditorState::IsAssetSelected(AssetID asset) const {

	return selectionKind == EditorSelectionKind::Asset && selectedAsset == asset;
}

bool Engine::EditorState::HasValidSelection(ECSWorld* world) const {

	if (selectionKind == EditorSelectionKind::Asset) {
		return static_cast<bool>(selectedAsset);
	}

	if (!world || !world->IsAlive(selectedEntity)) {
		return false;
	}

	if (selectionKind == EditorSelectionKind::MeshSubMesh) {
		return HasValidSubMeshSelection(world);
	}
	return selectionKind == EditorSelectionKind::Entity;
}

void Engine::EditorState::ClearSelection() {

	selectionKind = EditorSelectionKind::None;
	selectedEntity = Entity::Null();
	selectedAsset = {};
	selectedSubMeshIndex = 0;
	selectedSubMeshStableID = UUID{};
}

void Engine::SceneViewCameraSelection::ClearAssignedCameras() {

	orthographicCameraUUID = UUID{};
	perspectiveCameraUUID = UUID{};
}
