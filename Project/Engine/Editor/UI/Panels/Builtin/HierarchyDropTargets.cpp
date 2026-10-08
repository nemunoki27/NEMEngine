#include "HierarchyDropTargets.h"
#include "HierarchyEntityOperations.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Commands/Entity/ReparentEntityCommand.h>
#include <Engine/Editor/Commands/Entity/ReparentEntitiesCommand.h>
#include <Engine/Editor/Commands/Entity/ReorderEntityCommand.h>
#include <Engine/Editor/Commands/Entity/InstantiatePrefabCommand.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Serialization/SceneCreationScope.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Editor/Utility/AssetEntityFactory.h>
#include <Engine/Editor/Utility/PrefabInstanceEditUtility.h>
#include <Engine/Editor/Commands/Entity/CreateDroppedEntityCommand.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <optional>
#include <vector>

using namespace Engine::HierarchyEntityOperations;

void Engine::HierarchyDropTargets::DropProjectAssetToHierarchy(const Engine::EditorPanelContext& context,
	Engine::ECSWorld& world, const Engine::EditorAssetDragDropPayload& payload, const Engine::Entity& parent) {

	if (!context.CanEditScene() || !context.editorContext || !context.editorContext->assetDatabase || !context.host) {
		return;
	}
	if (parent.IsValid() && !world.IsAlive(parent)) {
		return;
	}

	// プレファブは既存のコマンド経路を使い、Undo対応のままインスタンス化する
	if (payload.assetType == Engine::AssetType::Prefab) {

		const Engine::UUID parentUUID = world.IsAlive(parent) ? world.GetUUID(parent) : Engine::UUID{};
		context.host->ExecuteEditorCommand(std::make_unique<Engine::InstantiatePrefabCommand>(payload.assetID, parentUUID));
		return;
	}

	// 配置可能なAssetをファクトリで生成
	if (!Engine::AssetEntityFactory::CanSpawn(payload) || !context.graphicsCore) {
		return;
	}
	// 子として配置する場合は親のSceneへ所属させる
	UUID sceneInstanceID = context.editorContext->activeSceneInstanceID;
	if (world.IsAlive(parent)) {
		const auto* owner = world.TryGetComponent<SceneObjectComponent>(parent);
		if (!owner) {
			return;
		}
		sceneInstanceID = owner->sceneInstanceID;
	}
	SceneCreationScope creation(world);
	Engine::HierarchySystem hierarchySystem{};
	const Engine::AssetSpawnResult spawn = Engine::AssetEntityFactory::Spawn(
		world, *context.editorContext->assetDatabase, *context.graphicsCore, hierarchySystem, payload, sceneInstanceID);
	if (!spawn.valid) {
		return;
	}
	if (world.IsAlive(parent)) {
		if (!CanReparent(context, world, spawn.root, parent)) {
			return;
		}
		hierarchySystem.SetParent(world, spawn.root, parent);
	}
	// 履歴への登録後に生成物を確定
	if (!context.host->ExecuteEditorCommand(std::make_unique<Engine::CreateDroppedEntityCommand>(spawn.root))) {
		return;
	}
	creation.Commit();
	if (context.editorState) {
		context.editorState->SelectEntity(spawn.root);
	}
}
void Engine::HierarchyDropTargets::DrawSiblingDropTarget(
	const EditorPanelContext& context, ECSWorld& world, const Entity& anchorEntity, bool insertAfter) {

	if (!context.CanEditScene() || !world.IsAlive(anchorEntity)) {
		return;
	}
	const ImGuiPayload* activePayload = ImGui::GetDragDropPayload();
	if (!activePayload || !activePayload->IsDataType(IEditorPanel::kHierarchyDragDropPayloadType)) {
		return;
	}

	const UUID anchorUUID = world.GetUUID(anchorEntity);
	const std::string id = (insertAfter ? "##SiblingDropAfter" : "##SiblingDropBefore") + ToString(anchorUUID);

	ImGui::PushID(id.c_str());
	const ImVec2 size(ImGui::GetContentRegionAvail().x, 1.0f);
	ImGui::InvisibleButton("##SiblingDropLine", size);

	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kHierarchyDragDropPayloadType)) {
			if (payload->IsDelivery()) {

				Entity dragged = ResolveDraggedEntity(world, payload);
				if (CanReorder(context, world, dragged, anchorEntity)) {

					context.host->ExecuteEditorCommand(
						std::make_unique<ReorderEntityCommand>(dragged, anchorEntity, insertAfter));
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {

		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, (min.y + max.y) * 0.5f), ImVec2(max.x, (min.y + max.y) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
	}
	ImGui::PopID();
}

void Engine::HierarchyDropTargets::DrawRootDropTarget(const EditorPanelContext& context, ECSWorld& world) {

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::TextDisabled("エンティティをルートに戻す");
	ImGui::InvisibleButton("HierarchyRootDropTarget", ImVec2(ImGui::GetContentRegionAvail().x, 24.0f));

	// 編集可能なPanelだけでドロップを受け付ける
	if (!context.CanEditScene() || !context.host) {
		return;
	}
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kHierarchyDragDropPayloadType)) {
			if (payload->IsDelivery()) {

				// 親子変更が必要な選択ルートだけを収集
				std::vector<Entity> entities = ResolveDraggedEntities(context, world, payload);
				entities.erase(std::remove_if(entities.begin(), entities.end(),
								   [&](const Entity& entity) { return !CanReparent(context, world, entity, Entity::Null()); }),
					entities.end());
				if (!entities.empty()) {
					context.host->ExecuteEditorCommand(std::make_unique<ReparentEntitiesCommand>(std::move(entities), UUID{}));
				}
			}
		}
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kProjectAssetDragDropPayloadType)) {
			if (payload->IsDelivery() && context.CanEditScene() && payload->DataSize == sizeof(EditorAssetDragDropPayload)) {

				// 何にも重なっていない空き領域へのドロップはルートエンティティとして原点に作成する
				const auto* assetPayload = static_cast<const EditorAssetDragDropPayload*>(payload->Data);
				if (assetPayload) {
					DropProjectAssetToHierarchy(context, world, *assetPayload, Entity::Null());
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
}
