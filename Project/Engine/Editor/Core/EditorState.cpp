#include "EditorState.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticleEmitterComponent.h>
#include <Engine/Core/World/Components/Rendering/SkyboxRendererComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>

// c++
#include <algorithm>
#include <optional>

//============================================================================
//	EditorState internal
//============================================================================

// 複数選択の次元ガードやスナップグリッド描画用にエンティティの2D/3Dを判定する、確定できなければnullopt
// TextはMeshと違い2D/3D両対応なのでdimensionで切り替える
std::optional<Engine::Dimension> Engine::ResolveEntityDimension(ECSWorld& world, const Entity& entity) {

	if (!world.IsAlive(entity)) {
		return std::nullopt;
	}
	if (world.HasComponent<SpriteRendererComponent>(entity) ||
		world.HasComponent<OrthographicCameraComponent>(entity)) {
		return Dimension::Type2D;
	}
	if (world.HasComponent<TextRendererComponent>(entity)) {
		return world.GetComponent<TextRendererComponent>(entity).dimension;
	}
	// PrimitiveはPlane/RingをScreen2Dにしたときだけ2D、それ以外は3D
	if (world.HasComponent<PrimitiveRendererComponent>(entity)) {
		return IsPrimitiveScreen2D(world.GetComponent<PrimitiveRendererComponent>(entity)) ?
			Dimension::Type2D : Dimension::Type3D;
	}
	// ParticleEmitterはエフェクトの描画空間がScreen2Dのときだけ2D
	if (world.HasComponent<ParticleEmitterComponent>(entity)) {
		return world.GetComponent<ParticleEmitterComponent>(entity).runtimeRenderSettings.space == PrimitiveRenderSpace::Screen2D ?
			Dimension::Type2D : Dimension::Type3D;
	}
	if (world.HasComponent<MeshRendererComponent>(entity) ||
		world.HasComponent<PerspectiveCameraComponent>(entity) ||
		world.HasComponent<DirectionalLightComponent>(entity) ||
		world.HasComponent<PointLightComponent>(entity) ||
		world.HasComponent<SpotLightComponent>(entity)) {
		return Dimension::Type3D;
	}
	return std::nullopt;
}

std::string Engine::GetEntityDisplayName(ECSWorld& world, const Entity& entity) {

	if (world.HasComponent<NameComponent>(entity)) {

		const std::string& name = world.GetComponent<NameComponent>(entity).name;
		if (!name.empty()) {
			return name;
		}
	}
	return "Entity";
}

//============================================================================
//	EditorState classMethods
//============================================================================
void Engine::EditorState::ValidateSelection(ECSWorld* world) {

	// アセット選択はECSWorldに依存しない
	if (selectionKind == EditorSelectionKind::Asset) {
		return;
	}
	if (!world) {
		ClearSelection();
		return;
	}

	// ジョイント選択は複数選択を持たないので個別に検証する、無効化したらクリアする
	// SelectJointがselectedEntitiesを空にするため、ここで弾かないと次フレームにクリアされてしまう
	if (selectionKind == EditorSelectionKind::Joint) {

		if (!HasValidJointSelection(world)) {
			ClearSelection();
		}
		return;
	}

	// 死んだエンティティを複数選択から除去し、全滅ならクリア、アクティブは生存個体へ寄せる
	selectedEntities.erase(std::remove_if(selectedEntities.begin(), selectedEntities.end(),
		[&](const Entity& entity) { return !world->IsAlive(entity); }), selectedEntities.end());
	if (selectedEntities.empty()) {
		ClearSelection();
		return;
	}
	if (!world->IsAlive(selectedEntity)) {
		selectedEntity = selectedEntities.back();
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
	selectedJointSkinnedEntity = Entity::Null();
	selectedJointIndex = -1;
	selectedEntities.clear();
	if (entity.IsValid()) {
		selectedEntities.push_back(entity);
	}
}

void Engine::EditorState::SelectJoint(const Entity& skinnedEntity, int32_t jointIndex) {

	const bool valid = skinnedEntity.IsValid() && jointIndex >= 0;
	selectionKind = valid ? EditorSelectionKind::Joint : EditorSelectionKind::None;
	selectedJointSkinnedEntity = skinnedEntity;
	selectedJointIndex = jointIndex;
	// 文脈としてスキンメッシュエンティティを選択扱いにしておく、インスペクターはJoint種別で分岐する
	selectedEntity = skinnedEntity;
	selectedAsset = {};
	selectedSubMeshIndex = 0;
	selectedSubMeshStableID = UUID{};
	selectedEntities.clear();
}

bool Engine::EditorState::HasValidJointSelection(ECSWorld* world) const {

	if (!world || selectionKind != EditorSelectionKind::Joint) {
		return false;
	}
	if (!world->IsAlive(selectedJointSkinnedEntity) || selectedJointIndex < 0) {
		return false;
	}
	if (!world->HasComponent<SkinnedAnimationComponent>(selectedJointSkinnedEntity)) {
		return false;
	}
	const auto& anim = world->GetComponent<SkinnedAnimationComponent>(selectedJointSkinnedEntity);
	return selectedJointIndex < static_cast<int32_t>(anim.runtimeSkeleton.joints.size());
}

bool Engine::EditorState::IsJointSelected(const Entity& skinnedEntity, int32_t jointIndex) const {

	return selectionKind == EditorSelectionKind::Joint &&
		selectedJointSkinnedEntity == skinnedEntity && selectedJointIndex == jointIndex;
}

void Engine::EditorState::AddEntityToSelection(const Entity& entity) {

	if (!entity.IsValid()) {
		return;
	}
	selectionKind = EditorSelectionKind::Entity;
	selectedAsset = {};
	selectedSubMeshIndex = 0;
	selectedSubMeshStableID = UUID{};
	if (std::find(selectedEntities.begin(), selectedEntities.end(), entity) == selectedEntities.end()) {
		selectedEntities.push_back(entity);
	}
	// アクティブは最後に触れたエンティティにする
	selectedEntity = entity;
}

void Engine::EditorState::ToggleEntityInSelection(const Entity& entity) {

	if (!entity.IsValid()) {
		return;
	}
	auto it = std::find(selectedEntities.begin(), selectedEntities.end(), entity);
	if (it != selectedEntities.end()) {

		selectedEntities.erase(it);
		if (selectedEntities.empty()) {
			ClearSelection();
			return;
		}
		selectionKind = EditorSelectionKind::Entity;
		selectedEntity = selectedEntities.back();
		return;
	}
	AddEntityToSelection(entity);
}

void Engine::EditorState::SetSelectedEntities(const std::vector<Entity>& entities) {

	selectedEntities.clear();
	for (const Entity& entity : entities) {

		if (entity.IsValid() &&
			std::find(selectedEntities.begin(), selectedEntities.end(), entity) == selectedEntities.end()) {
			selectedEntities.push_back(entity);
		}
	}
	if (selectedEntities.empty()) {
		ClearSelection();
		return;
	}
	selectionKind = EditorSelectionKind::Entity;
	selectedAsset = {};
	selectedSubMeshIndex = 0;
	selectedSubMeshStableID = UUID{};
	selectedEntity = selectedEntities.back();
}

bool Engine::EditorState::CanMultiSelect(ECSWorld& world, const Entity& candidate) const {

	// Skyboxは背景専用なので複数選択の対象外にして単一選択のみ許す
	if (world.HasComponent<SkyboxRendererComponent>(candidate)) {
		return false;
	}
	for (const Entity& entity : selectedEntities) {
		if (world.IsAlive(entity) && world.HasComponent<SkyboxRendererComponent>(entity)) {
			return false;
		}
	}
	if (selectedEntities.empty()) {
		return true;
	}
	const std::optional<Dimension> candidateDim = ResolveEntityDimension(world, candidate);
	if (!candidateDim) {
		return true;
	}
	// 既存選択の確定次元と食い違ったら追加させない
	for (const Entity& entity : selectedEntities) {

		const std::optional<Dimension> dim = ResolveEntityDimension(world, entity);
		if (dim && *dim != *candidateDim) {
			return false;
		}
	}
	return true;
}

void Engine::EditorState::SelectMeshSubMesh(const Entity& entity, uint32_t subMeshIndex, UUID stableID) {

	selectionKind = entity.IsValid() ? EditorSelectionKind::MeshSubMesh : EditorSelectionKind::None;
	selectedEntity = entity;
	selectedAsset = {};
	selectedSubMeshIndex = subMeshIndex;
	selectedSubMeshStableID = stableID;
	selectedJointSkinnedEntity = Entity::Null();
	selectedJointIndex = -1;
	selectedEntities.clear();
	if (entity.IsValid()) {
		selectedEntities.push_back(entity);
	}
}

void Engine::EditorState::SelectAsset(AssetID asset) {

	selectionKind = asset ? EditorSelectionKind::Asset : EditorSelectionKind::None;
	selectedAsset = asset;
	++assetSelectionRevision;
	selectedEntity = Entity::Null();
	selectedEntities.clear();
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

void Engine::EditorState::CommitScenePick(ECSWorld& world) {

	// ドラッグせず離したクリックだけ確定する
	if (!scenePickClickPending) {
		return;
	}
	scenePickClickPending = false;

	// 候補が生存していれば選択、空の場所をクリックしたなら選択解除する
	if (world.IsAlive(scenePickDragEntity)) {
		if (scenePickClickAdditive && selectKind == EditorSelectionKind::Entity &&
			CanMultiSelect(world, scenePickDragEntity)) {
			ToggleEntityInSelection(scenePickDragEntity);
		} else {
			SelectFromScenePick(scenePickDragEntity, scenePickCandidateSubMesh, scenePickCandidateSubMeshID);
		}
	} else if (!scenePickClickAdditive) {
		ClearSelection();
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

	if (selectionKind != EditorSelectionKind::Entity) {
		return false;
	}
	return std::find(selectedEntities.begin(), selectedEntities.end(), entity) != selectedEntities.end();
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
	if (selectionKind == EditorSelectionKind::Joint) {
		return HasValidJointSelection(world);
	}
	return selectionKind == EditorSelectionKind::Entity;
}

void Engine::EditorState::ClearSelection() {

	selectionKind = EditorSelectionKind::None;
	selectedEntity = Entity::Null();
	selectedEntities.clear();
	selectedAsset = {};
	selectedSubMeshIndex = 0;
	selectedSubMeshStableID = UUID{};
	selectedJointSkinnedEntity = Entity::Null();
	selectedJointIndex = -1;
}

void Engine::SceneViewCameraSelection::ClearAssignedCameras() {

	orthographicCameraUUID = UUID{};
	perspectiveCameraUUID = UUID{};
}
