#include "HierarchyPanel.h"
#include "HierarchyEntityOperations.h"
#include "HierarchyDropTargets.h"
#include <Engine/Editor/UI/Common/EntityCreationMenu.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentUtility.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <optional>
#include <vector>

using namespace Engine::HierarchyEntityOperations;
using namespace Engine::EntityCreationMenu;

//============================================================================
//	HierarchyPanel classMethods
//============================================================================
namespace {

	// 兄弟の表示順を取得
	int32_t GetHierarchySiblingOrder(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return 0;
		}
		return world.GetComponent<Engine::HierarchyComponent>(entity).siblingOrder;
	}

}

Engine::HierarchyPanel::HierarchyPanel(TextureUploadService& textureUploadService) : entityTree_(textureUploadService) {
}

void Engine::HierarchyPanel::Draw(const EditorPanelContext& context) {

	// ヒエラルキーパネルの表示状態を確認
	if (!context.layoutState->showHierarchy) {
		return;
	}

	const bool visible = ImGui::Begin("Hierarchy", &context.layoutState->showHierarchy);
	if (context.host && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
		context.host->NotifyEditorCommandPanelFocused(EditorCommandPanelKind::Scene);
	}
	if (!visible) {
		ImGui::End();
		return;
	}

	// プレファブ編集中はバナーを出し、戻るボタンで一回の操作で元のシーン編集へ戻る
	if (context.editorContext && context.editorContext->isPrefabEditing) {

		if (ImGui::Button("< 戻る") && context.host) {
			context.host->RequestExitPrefabEditAll();
		}
		ImGui::SameLine();
		if (context.editorContext->isPrefabInContext) {
			ImGui::Text("Prefab: %s  (In-Context)", context.editorContext->prefabEditName.c_str());
		} else {
			ImGui::Text("Prefab: %s", context.editorContext->prefabEditName.c_str());
		}
		ImGui::Separator();
	}

	// 検索欄の左端にProjectPanelと同じ虫眼鏡アイコンを重ねる
	entityTree_.DrawSearch();

	ImGui::Separator();

	//============================================================================
	//	ワールドのルートエンティティを列挙して表示
	//============================================================================
	DrawBackgroundContextMenu(context);

	ECSWorld* world = context.GetWorld();
	if (!world) {
		ImGui::TextDisabled("Active World is null.");
		ImGui::End();
		return;
	}

	// プレファブ編集中の表示制御、隔離編集は環境を隠し、In-Context編集は編集中のプレファブだけを出す
	const bool prefabEditing = context.editorContext && context.editorContext->isPrefabEditing;
	const bool inContext = context.editorContext && context.editorContext->isPrefabInContext;
	const UUID inContextInstanceID = context.editorContext ? context.editorContext->prefabInContextInstanceID : UUID{};
	const std::vector<Entity>* environmentEntities =
		context.editorContext ? context.editorContext->prefabEnvironmentEntities : nullptr;

	std::vector<Entity> rootEntities;
	rootEntities.reserve(world->GetRecordCount());
	world->ForEachAliveEntity([&](Entity entity) {
		// シーン編集対象でない内部Entityは表示しない
		if (!world->HasComponent<SceneObjectComponent>(entity)) {
			return;
		}
		// ルートエンティティでない場合はスキップ
		if (!IsRootEntity(*world, entity)) {
			return;
		}
		// 解決できるジョイント接続はルート一覧に出さず、ジョイント直下に表示する
		if (world->HasComponent<JointAttachmentComponent>(entity)) {

			Entity skinned = Entity::Null();
			Matrix4x4 jointSkeletonSpace{};
			if (JointAttachmentUtility::ResolveAttachedJoint(*world, entity, skinned, jointSkeletonSpace)) {
				return;
			}
		}
		// In-Context編集は描画だけ元シーンを残し、ヒエラルキーは編集中インスタンスのルートだけに絞る
		if (prefabEditing && inContext) {

			if (!world->HasComponent<PrefabLinkComponent>(entity) ||
				world->GetComponent<PrefabLinkComponent>(entity).prefabInstanceID != inContextInstanceID) {
				return;
			}
		} else if (prefabEditing && environmentEntities) {

			if (std::find(environmentEntities->begin(), environmentEntities->end(), entity) != environmentEntities->end()) {
				return;
			}
		}

		rootEntities.emplace_back(entity);
	});
	std::stable_sort(rootEntities.begin(), rootEntities.end(), [&](const Entity& lhs, const Entity& rhs) {
		return GetHierarchySiblingOrder(*world, lhs) < GetHierarchySiblingOrder(*world, rhs);
	});

	bool hasVisibleEntity = false;
	bool entitySectionOpen = true;
	entityTree_.BeginFrame();
	auto drawSceneRoots = [&](UUID sceneInstanceID) {
		Entity lastVisibleRoot = Entity::Null();
		for (const Entity& entity : rootEntities) {

			if (world->GetComponent<SceneObjectComponent>(entity).sceneInstanceID != sceneInstanceID ||
				!entityTree_.ShouldDrawEntityNode(*world, entity)) {
				continue;
			}

			HierarchyDropTargets::DrawSiblingDropTarget(context, *world, entity, false);
			entityTree_.DrawEntityNode(context, *world, entity, false);
			hasVisibleEntity = true;
			lastVisibleRoot = entity;
		}
		if (world->IsAlive(lastVisibleRoot)) {
			HierarchyDropTargets::DrawSiblingDropTarget(context, *world, lastVisibleRoot, true);
		}
	};

	SceneInstanceManager* sceneInstances = context.editorContext ? context.editorContext->sceneInstances : nullptr;
	if (sceneInstances && !prefabEditing && !sceneInstances->GetAll().empty()) {

		entitySectionOpen = false;
		for (const SceneInstance& scene : sceneInstances->GetAll()) {

			ImGui::PushID(ToString(scene.instanceID).c_str());
			const bool open = MyGUI::CollapsingHeader(scene.header.name.c_str(), true);
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
				sceneInstances->SetActive(scene.instanceID);
			}
			if (open) {
				entitySectionOpen = true;
				drawSceneRoots(scene.instanceID);
			}
			ImGui::PopID();
		}
	} else {
		for (const Entity& entity : rootEntities) {
			if (!entityTree_.ShouldDrawEntityNode(*world, entity)) {
				continue;
			}
			HierarchyDropTargets::DrawSiblingDropTarget(context, *world, entity, false);
			entityTree_.DrawEntityNode(context, *world, entity, false);
			hasVisibleEntity = true;
		}
	}
	if (entitySectionOpen) {
		if (rootEntities.empty()) {
			ImGui::TextDisabled("エンティティなし");
		} else if (!hasVisibleEntity) {
			ImGui::TextDisabled("該当なし");
		}
	}

	// 親子関係のないエンティティをドロップしてルートエンティティにするためのドロップ目標
	HierarchyDropTargets::DrawRootDropTarget(context, *world);

	ImGui::End();
}

void Engine::HierarchyPanel::DrawBackgroundContextMenu(const EditorPanelContext& context) {

	//============================================================================
	//	右クリックのコンテキストメニュー
	//============================================================================
	if (ImGui::BeginPopupContextWindow(
			"HierarchyWindowContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {

		DrawEntityCreationMenu(context, UUID{}, "オブジェクトを作成",
			ResolveSceneViewCameraDimension(context.editorState->sceneViewPickDimension));
		// コピーエンティティを作成
		const bool canPaste = context.editorState && context.editorState->HasClipboard() && context.CanEditScene();
		if (ImGui::MenuItem("コピー済みをペースト", "Ctrl+V", false, canPaste)) {

			context.host->PasteClipboard();
		}
		ImGui::EndPopup();
	}
}
