#include "HierarchyPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/Interface/IEditorPanelHost.h>
#include <Engine/Editor/Command/Methods/CreateEntityCommand.h>
#include <Engine/Editor/Command/Methods/DeleteEntityCommand.h>
#include <Engine/Editor/Command/Methods/ReparentEntityCommand.h>
#include <Engine/Editor/Command/Methods/DuplicateEntityCommand.h>
#include <Engine/Editor/Command/Methods/SetEntityActiveCommand.h>
#include <Engine/Editor/Command/Methods/InstantiatePrefabCommand.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
#include <Engine/Core/Graphics/Texture/GPUTextureResource.h>
#include <Engine/Core/Graphics/Texture/TextureUploadService.h>
#include <Engine/Core/UUID/UUID.h>
#include <Engine/Utility/ImGui/MyGUI.h>

//============================================================================
//	HierarchyPanel classMethods
//============================================================================

namespace {

	constexpr const char* kActiveEyeTextureKey = "editor:hierarchy:entityActiveEye";
	constexpr const char* kInactiveEyeTextureKey = "editor:hierarchy:entityActiveOffEye";
	constexpr const char* kActiveEyeTexturePath =
		"Engine/Assets/Textures/Engine/Editor/Hierarchy/entityActiveEye.dds";
	constexpr const char* kInactiveEyeTexturePath =
		"Engine/Assets/Textures/Engine/Editor/Hierarchy/entityActiveOffEye.dds";
}

Engine::HierarchyPanel::HierarchyPanel(TextureUploadService& textureUploadService) :
	textureUploadService_(&textureUploadService) {
}

void Engine::HierarchyPanel::Draw(const EditorPanelContext& context) {

	// ヒエラルキーパネルの表示状態を確認
	if (!context.layoutState->showHierarchy) {
		return;
	}

	if (!ImGui::Begin("Hierarchy", &context.layoutState->showHierarchy)) {
		ImGui::End();
		return;
	}

	RequestActiveIconTextures();

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

	bool hasAnyRoot = false;
	world->ForEachAliveEntity([&](Entity entity) {

		// ルートエンティティでない場合はスキップ
		if (!IsRootEntity(*world, entity)) {
			return;
		}

		hasAnyRoot = true;
		// ルートエンティティを表示
		DrawEntityNode(context, *world, entity);
		});
	if (!hasAnyRoot) {
		ImGui::TextDisabled("Hierarchy is empty.");
	}

	// 親子関係のないエンティティをドロップしてルートエンティティにするためのドロップ目標
	DrawRootDropTarget(context, *world);

	ImGui::End();
}

void Engine::HierarchyPanel::RequestActiveIconTextures() {

	if (activeIconRequested_ || !textureUploadService_) {
		return;
	}

	textureUploadService_->RequestTextureFile(kActiveEyeTextureKey, kActiveEyeTexturePath);
	textureUploadService_->RequestTextureFile(kInactiveEyeTextureKey, kInactiveEyeTexturePath);
	activeIconRequested_ = true;
}

void Engine::HierarchyPanel::DrawActiveToggleIcon(const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool activeSelf, bool& leftClicked, bool& rightClicked) {

	leftClicked = false;
	rightClicked = false;

	ImTextureID textureID{};
	if (textureUploadService_) {
		const char* textureKey = activeSelf ? kActiveEyeTextureKey : kInactiveEyeTextureKey;
		if (const auto* texture = textureUploadService_->GetTexture(textureKey)) {
			textureID = static_cast<ImTextureID>(texture->gpuHandle.ptr);
		}
	}

	const float iconSize = ImGui::GetTextLineHeight() * 0.92f;
	const ImVec2 buttonSize(iconSize, iconSize);

	if (!context.CanEditScene()) {
		ImGui::BeginDisabled();
	}

	bool toggled = false;
	if (textureID != ImTextureID{}) {

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.24f, 0.28f, 0.75f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.34f, 0.40f, 0.95f));
		toggled = ImGui::ImageButton("##ActiveEye", textureID, buttonSize);
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();
	} else {

		bool editedActiveSelf = activeSelf;
		toggled = MyGUI::SmallCheckbox("##Active", editedActiveSelf);
	}

	leftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

	if (!context.CanEditScene()) {
		ImGui::EndDisabled();
	}

	if (toggled && world.IsAlive(entity)) {

		context.host->ExecuteEditorCommand(std::make_unique<SetEntityActiveCommand>(entity, !activeSelf));
	}
}

void Engine::HierarchyPanel::DrawEntityNode(const EditorPanelContext& context,
	ECSWorld& world,
	const Entity& entity) {

	// アクティブ状態を取得
	bool activeSelf = true;
	bool activeInHierarchy = true;
	if (world.HasComponent<SceneObjectComponent>(entity)) {

		const auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
		activeSelf = sceneObject.activeSelf;
		activeInHierarchy = sceneObject.activeInHierarchy;
	}

	// 選択されているか
	bool isSelected = context.editorState && context.editorState->IsEntitySelected(entity);
	// 階層コンポーネントを持っているか
	bool hasHierarchy = world.HasComponent<HierarchyComponent>(entity);
	// 子を持っているか
	bool hasChildren = false;
	Entity firstChild = Entity::Null();
	if (hasHierarchy) {

		const auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
		firstChild = hierarchy.firstChild;
		hasChildren = world.IsAlive(firstChild);
	}
	// サブメッシュを持っているか
	bool hasSubMeshChildren = false;
	if (world.HasComponent<MeshRendererComponent>(entity)) {

		const auto& meshRenderer = world.GetComponent<MeshRendererComponent>(entity);
		hasSubMeshChildren = !meshRenderer.subMeshes.empty();
	}

	// ツリー表示できる子がいるか
	bool hasAnyTreeChildren = hasChildren || hasSubMeshChildren;

	// ノードのフラグを設定
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (isSelected) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (!hasAnyTreeChildren) {
		flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	// 表示名を取得
	const std::string displayName = GetEntityDisplayName(world, entity);
	const std::string idString = ToString(world.GetUUID(entity));

	ImGui::PushID(idString.c_str());

	//============================================================================
	//	左側のアクティブチェックボックス
	//============================================================================

	// チェックボックスがクリックされたか
	bool checkboxLeftClicked = false;
	bool checkboxRightClicked = false;
	DrawActiveToggleIcon(context, world, entity, activeSelf, checkboxLeftClicked, checkboxRightClicked);

	// チェックボックスクリックでも選択状態にする
	if (checkboxLeftClicked || checkboxRightClicked) {
		context.editorState->SelectEntity(entity);
	}

	// チェックボックス上で右クリックしたときもコンテキストメニューを開く
	if (checkboxRightClicked) {
		ImGui::OpenPopup("HierarchyEntityContextMenu");
	}

	ImGui::SameLine(0.0f, 7.0f);

	//============================================================================
	//	ツリーノード本体
	//============================================================================

	// アクティブでない場合はテキストを薄く表示する
	if (!activeInHierarchy) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	}

	const ImGuiStyle& style = ImGui::GetStyle();
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 1.0f));
	ImGui::SetWindowFontScale(0.88f);
	bool opened = ImGui::TreeNodeEx("##HierarchyNode", flags, "%s", displayName.c_str());
	ImGui::SetWindowFontScale(1.0f);
	ImGui::PopStyleVar(2);

	if (!activeInHierarchy) {
		ImGui::PopStyleColor();
	}

	// ノードがクリックされたか
	bool nodeLeftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	bool nodeRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

	// ノードがクリックされたら選択状態にする
	if (nodeLeftClicked || nodeRightClicked) {
		context.editorState->SelectEntity(entity);
	}

	// ノード右クリックでもコンテキストメニューを開く
	if (nodeRightClicked) {
		ImGui::OpenPopup("HierarchyEntityContextMenu");
	}

	//============================================================================
	//	右クリックのコンテキストメニュー
	//============================================================================
	if (ImGui::BeginPopup("HierarchyEntityContextMenu")) {

		context.editorState->SelectEntity(entity);

		// アクティブ切り替え
		if (ImGui::MenuItem(activeSelf ? "Set Inactive" : "Set Active", nullptr, false, context.CanEditScene())) {

			context.host->ExecuteEditorCommand(
				std::make_unique<SetEntityActiveCommand>(entity, !activeSelf));
		}

		ImGui::Separator();

		// 子に空エンティティを追加
		if (ImGui::MenuItem("Create Empty Child", nullptr, false, context.CanEditScene())) {

			context.host->ExecuteEditorCommand(
				std::make_unique<CreateEntityCommand>("Entity", world.GetUUID(entity)));
		}
		// エンティティを複製
		if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, context.CanEditScene())) {

			context.host->DuplicateSelection();
		}
		// クリップボードにエンティティをコピー
		if (ImGui::MenuItem("Copy", "Ctrl+C", false, context.CanEditScene())) {

			context.host->CopySelectionToClipboard();
		}
		// エンティティを削除
		if (ImGui::MenuItem("Delete", "Del", false, context.CanEditScene())) {

			context.host->ExecuteEditorCommand(std::make_unique<DeleteEntityCommand>(entity));
		}
		ImGui::EndPopup();
	}

	//============================================================================
	//	ドラッグ開始
	//============================================================================
	if (ImGui::BeginDragDropSource()) {

		const UUID stableUUID = world.GetUUID(entity);
		ImGui::SetDragDropPayload(kHierarchyDragDropPayloadType, &stableUUID, sizeof(UUID));
		ImGui::Text("%s", displayName.c_str());
		ImGui::EndDragDropSource();
	}

	//============================================================================
	//	ドラッグ目標
	//============================================================================
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyDragDropPayloadType)) {
			if (payload->IsDelivery()) {

				Entity dragged = ResolveDraggedEntity(world, payload);
				if (CanReparent(world, dragged, entity)) {

					context.host->ExecuteEditorCommand(
						std::make_unique<ReparentEntityCommand>(dragged, world.GetUUID(entity)));
				}
			}
		}
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectAssetDragDropPayloadType)) {
			if (payload->IsDelivery() && context.CanEditScene() && payload->DataSize == sizeof(EditorAssetDragDropPayload)) {

				const auto* assetPayload = static_cast<const EditorAssetDragDropPayload*>(payload->Data);
				if (assetPayload && assetPayload->assetType == AssetType::Prefab) {

					context.host->ExecuteEditorCommand(
						std::make_unique<InstantiatePrefabCommand>(assetPayload->assetID, world.GetUUID(entity)));
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	//============================================================================
	//	子ノードの表示
	//============================================================================
	if (hasAnyTreeChildren && opened) {

		Entity child = firstChild;
		while (child.IsValid() && world.IsAlive(child)) {

			DrawEntityNode(context, world, child);
			if (!world.HasComponent<HierarchyComponent>(child)) {
				break;
			}
			child = world.GetComponent<HierarchyComponent>(child).nextSibling;
		}

		// サブメッシュノードの表示
		if (hasSubMeshChildren) {

			ImGui::SetWindowFontScale(0.72f);

			DrawSubMeshNodes(context, world, entity);

			ImGui::SetWindowFontScale(1.0f);
		}
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void Engine::HierarchyPanel::DrawSubMeshNodes(const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity) {

	// MeshRendererを持っていなければ何もしない
	if (!world.HasComponent<MeshRendererComponent>(entity)) {
		return;
	}
	const auto& meshRenderer = world.GetComponent<MeshRendererComponent>(entity);
	if (meshRenderer.subMeshes.empty()) {
		return;
	}

	ImGui::PushID("SubMeshesRoot");
	ImGui::Indent();
	if (MyGUI::CollapsingHeader("SubMeshes", false)) {

		ImGui::Indent();
		for (uint32_t subMeshIndex = 0; subMeshIndex < static_cast<uint32_t>(meshRenderer.subMeshes.size()); ++subMeshIndex) {

			const auto& subMesh = meshRenderer.subMeshes[subMeshIndex];

			// 選択状態
			bool isSelected = context.editorState && context.editorState->IsMeshSubMeshSelected(entity, subMesh.stableID, subMeshIndex);

			ImGui::PushID(static_cast<int>(subMeshIndex));

			// 表示名を決定
			std::string displayName = subMesh.name.empty() ?
				("SubMesh_" + std::to_string(subMesh.sourceSubMeshIndex)) : subMesh.name;
			std::string rowLabel = displayName + "##HierarchySubMeshRow";
			float rowWidth = ImGui::GetContentRegionAvail().x;
			// アクティブでない場合はテキストを薄く表示する
			if (!isSelected) {

				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			}

			const bool clicked = ImGui::Selectable(rowLabel.c_str(), isSelected, 0, ImVec2(rowWidth, 0.0f));
			if (!isSelected) {
				ImGui::PopStyleColor();
			}
			if (clicked) {

				context.editorState->SelectMeshSubMesh(entity, subMeshIndex, subMesh.stableID);
			}
			ImGui::PopID();
		}
		ImGui::Unindent();
	}
	ImGui::Unindent();
	ImGui::PopID();
}

void Engine::HierarchyPanel::DrawBackgroundContextMenu(const EditorPanelContext& context) {

	//============================================================================
	//	右クリックのコンテキストメニュー
	//============================================================================
	if (ImGui::BeginPopupContextWindow("HierarchyWindowContextMenu",
		ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {

		// 空エンティティを追加
		if (ImGui::MenuItem("Create Empty", nullptr, false, context.CanEditScene())) {

			context.host->ExecuteEditorCommand(std::make_unique<CreateEntityCommand>("Entity"));
		}
		// コピーエンティティを作成
		const bool canPaste = context.editorState && context.editorState->HasClipboard() && context.CanEditScene();
		if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste)) {

			context.host->PasteClipboard();
		}
		ImGui::EndPopup();
	}
}

void Engine::HierarchyPanel::DrawRootDropTarget(const EditorPanelContext& context, ECSWorld& world) {

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::TextDisabled("Drop here to make root");
	ImGui::InvisibleButton("HierarchyRootDropTarget", ImVec2(ImGui::GetContentRegionAvail().x, 24.0f));

	//============================================================================
	//	ドラッグ目標
	//============================================================================
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyDragDropPayloadType)) {
			if (payload->IsDelivery()) {

				// ドロップされたペイロードから目標エンティティを取得
				Entity dragged = ResolveDraggedEntity(world, payload);
				if (world.IsAlive(dragged)) {

					// ドロップされたエンティティの現在の親を取得
					Entity currentParent = Entity::Null();
					if (world.HasComponent<HierarchyComponent>(dragged)) {
						currentParent = world.GetComponent<HierarchyComponent>(dragged).parent;
					}

					// すでにルートなら何もしない
					if (world.IsAlive(currentParent)) {

						context.host->ExecuteEditorCommand(std::make_unique<ReparentEntityCommand>(dragged, UUID{}));
					}
				}
			}
		}
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectAssetDragDropPayloadType)) {
			if (payload->IsDelivery() && context.CanEditScene() && payload->DataSize == sizeof(EditorAssetDragDropPayload)) {

				const auto* assetPayload = static_cast<const EditorAssetDragDropPayload*>(payload->Data);
				if (assetPayload && assetPayload->assetType == AssetType::Prefab) {

					context.host->ExecuteEditorCommand(
						std::make_unique<InstantiatePrefabCommand>(assetPayload->assetID));
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
}

bool Engine::HierarchyPanel::IsRootEntity(ECSWorld& world, const Entity& entity) const {

	if (!world.HasComponent<HierarchyComponent>(entity)) {
		return true;
	}

	const auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
	return !world.IsAlive(hierarchy.parent);
}

bool Engine::HierarchyPanel::CanReparent(ECSWorld& world, const Entity& child, const Entity& newParent) const {

	// どちらも有効なエンティティでなければならない
	if (!world.IsAlive(child) || !world.IsAlive(newParent)) {
		return false;
	}
	if (child == newParent) {
		return false;
	}

	// 自分自身の子孫の下には入れられない
	Entity cursor = newParent;
	while (world.IsAlive(cursor)) {

		if (cursor == child) {
			return false;
		}
		if (!world.HasComponent<HierarchyComponent>(cursor)) {
			break;
		}
		// 親をたどる
		cursor = world.GetComponent<HierarchyComponent>(cursor).parent;
	}

	// すでにその親なら意味ないので処理しない
	if (world.HasComponent<HierarchyComponent>(child)) {
		const auto& hierarchy = world.GetComponent<HierarchyComponent>(child);
		if (hierarchy.parent == newParent) {
			return false;
		}
	}
	return true;
}

Engine::Entity Engine::HierarchyPanel::ResolveDraggedEntity(ECSWorld& world, const ImGuiPayload* payload) const {

	if (!payload || payload->DataSize != sizeof(UUID)) {
		return Entity::Null();
	}

	// ペイロードからUUIDを取得してエンティティを検索
	const UUID stableUUID = *static_cast<const UUID*>(payload->Data);
	return world.FindByUUID(stableUUID);
}

std::string Engine::HierarchyPanel::GetEntityDisplayName(ECSWorld& world, const Entity& entity) const {

	if (world.HasComponent<NameComponent>(entity)) {
		return world.GetComponent<NameComponent>(entity).name;
	}
	return "Entity";
}
