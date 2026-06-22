#include "HierarchyPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Commands/Entity/CreateEntityCommand.h>
#include <Engine/Editor/Commands/Entity/DeleteEntityCommand.h>
#include <Engine/Editor/Commands/Entity/ReparentEntityCommand.h>
#include <Engine/Editor/Commands/Entity/DuplicateEntityCommand.h>
#include <Engine/Editor/Commands/Entity/SetEntityActiveCommand.h>
#include <Engine/Editor/Commands/Entity/InstantiatePrefabCommand.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <vector>

//============================================================================
//	HierarchyPanel classMethods
//============================================================================
namespace {

	constexpr const char* kActiveEyeTextureKey = "editor:hierarchy:entityActiveEye";
	constexpr const char* kInactiveEyeTextureKey = "editor:hierarchy:entityActiveOffEye";

	int32_t GetHierarchySiblingOrder(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return 0;
		}
		return world.GetComponent<Engine::HierarchyComponent>(entity).siblingOrder;
	}
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

	// 検索欄の左端にProjectPanelと同じ虫眼鏡アイコンを重ねる
	const ImTextureID searchIcon = EditorTextureHelper::GetSearchIcon(*textureUploadService_);
	searchFilter_.DrawInput("##HierarchySearch", searchIcon, "検索...");

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

	std::vector<Entity> rootEntities;
	rootEntities.reserve(world->GetRecordCount());
	world->ForEachAliveEntity([&](Entity entity) {

		// ルートエンティティでない場合はスキップ
		if (!IsRootEntity(*world, entity)) {
			return;
		}

		rootEntities.emplace_back(entity);
		});
	std::stable_sort(rootEntities.begin(), rootEntities.end(), [&](const Entity& lhs, const Entity& rhs) {
		return GetHierarchySiblingOrder(*world, lhs) < GetHierarchySiblingOrder(*world, rhs);
		});

	bool hasVisibleEntity = false;
	Entity lastVisibleRoot = Entity::Null();
	for (const Entity& entity : rootEntities) {

		if (!ShouldDrawEntityNode(*world, entity)) {
			continue;
		}

		DrawSiblingDropTarget(context, *world, entity, false);
		// ルートエンティティを表示
		DrawEntityNode(context, *world, entity, false);
		hasVisibleEntity = true;
		lastVisibleRoot = entity;
	}
	if (world->IsAlive(lastVisibleRoot)) {

		DrawSiblingDropTarget(context, *world, lastVisibleRoot, true);
	}
	if (rootEntities.empty()) {
		ImGui::TextDisabled("Hierarchy is empty.");
	} else if (!hasVisibleEntity) {
		ImGui::TextDisabled("No matching entities.");
	}

	// 親子関係のないエンティティをドロップしてルートエンティティにするためのドロップ目標
	DrawRootDropTarget(context, *world);

	ImGui::End();
}

void Engine::HierarchyPanel::RequestActiveIconTextures() {

	if (activeIconRequested_ || !textureUploadService_) {
		return;
	}

	textureUploadService_->RequestTextureFile(kActiveEyeTextureKey,
		EditorTextureHelper::MakeEditorTexturePath("Hierarchy", "entityActiveEye.dds"));
	textureUploadService_->RequestTextureFile(kInactiveEyeTextureKey,
		EditorTextureHelper::MakeEditorTexturePath("Hierarchy", "entityActiveOffEye.dds"));
	activeIconRequested_ = true;
}

void Engine::HierarchyPanel::DrawActiveToggleIcon(const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool activeSelf, bool& leftClicked, bool& rightClicked) {

	leftClicked = false;
	rightClicked = false;

	ImTextureID textureID{};
	if (textureUploadService_) {
		const char* textureKey = activeSelf ? kActiveEyeTextureKey : kInactiveEyeTextureKey;
		textureID = EditorTextureHelper::GetImTextureID(*textureUploadService_, textureKey);
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

		// 選択中のエンティティなら全選択へ同じ状態を適用する
		const bool newActive = !activeSelf;
		if (context.editorState && context.editorState->IsEntitySelected(entity)) {
			for (const Entity& target : context.editorState->GetSelectedEntities()) {
				if (world.IsAlive(target)) {
					context.host->ExecuteEditorCommand(std::make_unique<SetEntityActiveCommand>(target, newActive));
				}
			}
		} else {
			context.host->ExecuteEditorCommand(std::make_unique<SetEntityActiveCommand>(entity, newActive));
		}
	}
}

void Engine::HierarchyPanel::DrawEntityNode(const EditorPanelContext& context,
	ECSWorld& world,
	const Entity& entity,
	bool forceVisible) {

	const bool selfMatchesSearch = EntityMatchesSearch(world, entity);
	const bool drawDescendants = forceVisible || selfMatchesSearch;

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
	if (searchFilter_.IsActive() && hasAnyTreeChildren) {
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}

	// 表示名を取得
	const std::string displayName = GetEntityDisplayName(world, entity);
	const std::string idString = ToString(world.GetUUID(entity));

	ImGui::PushID(idString.c_str());

	//============================================================================
	//	左側のアクティブチェックボックス
	//============================================================================
	// チェックボックスがクリックされたか
	// 左シフト併用はSceneViewと同じく次元が合えばトグルで追加選択する
	const bool additiveSelect = ImGui::IsKeyDown(ImGuiKey_LeftShift);
	// Ctrl併用時は選択を切り替えずにエンティティをドラッグできるようにする
	const bool ctrlHeld = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
	auto selectEntityInHierarchy = [&]() {
		if (additiveSelect && context.editorState->selectKind == EditorSelectionKind::Entity &&
			context.editorState->CanMultiSelect(world, entity)) {
			context.editorState->ToggleEntityInSelection(entity);
		} else {
			context.editorState->SelectEntity(entity);
		}
		};

	bool checkboxLeftClicked = false;
	bool checkboxRightClicked = false;
	DrawActiveToggleIcon(context, world, entity, activeSelf, checkboxLeftClicked, checkboxRightClicked);

	// チェックボックスクリックでも選択状態にする
	if (checkboxLeftClicked || checkboxRightClicked) {
		selectEntityInHierarchy();
	}

	// チェックボックス上で右クリックしたときもコンテキストメニューを開く
	if (checkboxRightClicked) {
		ImGui::OpenPopup("HierarchyEntityContextMenu");
	}

	ImGui::SameLine(0.0f, 7.0f);

	//============================================================================
	//	ツリーノード本体
	//============================================================================
	// アクティブでない場合はテキストを薄く表示する、プレファブインスタンスは水色で表示する
	// 非アクティブ表示を優先し、アクティブなプレファブインスタンスのみ水色にする
	const bool isPrefabInstance = world.HasComponent<PrefabLinkComponent>(entity);
	bool pushedTextColor = false;
	if (!activeInHierarchy) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		pushedTextColor = true;
	} else if (isPrefabInstance) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.80f, 1.0f, 1.0f));
		pushedTextColor = true;
	}

	const ImGuiStyle& style = ImGui::GetStyle();
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 1.0f));
	ImGui::SetWindowFontScale(0.88f);
	bool opened = ImGui::TreeNodeEx("##HierarchyNode", flags, "%s", displayName.c_str());
	ImGui::SetWindowFontScale(1.0f);
	ImGui::PopStyleVar(2);

	if (pushedTextColor) {
		ImGui::PopStyleColor();
	}

	// ノードがクリックされたか
	bool nodeLeftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	bool nodeRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

	// ノードがクリックされたら選択状態にする、右クリックで既に複数選択に含むなら維持する
	// Ctrl併用時は選択を変えずにドラッグだけ行えるよう選択をスキップする
	if (nodeLeftClicked && !ctrlHeld) {
		selectEntityInHierarchy();
	} else if (nodeRightClicked && !context.editorState->IsEntitySelected(entity)) {
		context.editorState->SelectEntity(entity);
	}

	// ダブルクリックでシーンカメラをそのエンティティへ寄せる
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		context.editorState->cameraFocusRequest = entity;
	}

	// ノード右クリックでもコンテキストメニューを開く
	if (nodeRightClicked) {
		ImGui::OpenPopup("HierarchyEntityContextMenu");
	}

	//============================================================================
	//	右クリックのコンテキストメニュー
	//============================================================================
	if (ImGui::BeginPopup("HierarchyEntityContextMenu")) {

		// 既に複数選択へ含まれているなら維持し、含まれていなければ単体選択にする
		if (!context.editorState->IsEntitySelected(entity)) {
			context.editorState->SelectEntity(entity);
		}

		// アクティブ切り替え、選択中なら全選択へ同じ状態を適用する
		if (ImGui::MenuItem(activeSelf ? "非アクティブにする" : "アクティブにする", nullptr, false, context.CanEditScene())) {

			const bool newActive = !activeSelf;
			if (context.editorState->IsEntitySelected(entity)) {
				for (const Entity& target : context.editorState->GetSelectedEntities()) {
					if (world.IsAlive(target)) {
						context.host->ExecuteEditorCommand(std::make_unique<SetEntityActiveCommand>(target, newActive));
					}
				}
			} else {
				context.host->ExecuteEditorCommand(std::make_unique<SetEntityActiveCommand>(entity, newActive));
			}
		}

		ImGui::Separator();

		// 子に空エンティティを追加
		if (ImGui::MenuItem("子に空オブジェクトを作成", nullptr, false, context.CanEditScene())) {

			context.host->ExecuteEditorCommand(
				std::make_unique<CreateEntityCommand>("Entity", world.GetUUID(entity)));
		}
		// エンティティを複製
		if (ImGui::MenuItem("複製", "Ctrl+D", false, context.CanEditScene())) {

			context.host->DuplicateSelection();
		}
		// クリップボードにエンティティをコピー
		if (ImGui::MenuItem("コピー", "Ctrl+C", false, context.CanEditScene())) {

			context.host->CopySelectionToClipboard();
		}
		// エンティティを削除、複数選択ならまとめて消す
		if (ImGui::MenuItem("削除", "Del", false, context.CanEditScene())) {

			const std::vector<Entity> targets = context.editorState->GetSelectedEntities();
			for (const Entity& target : targets) {
				if (world.IsAlive(target)) {
					context.host->ExecuteEditorCommand(std::make_unique<DeleteEntityCommand>(target));
				}
			}
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
		Entity lastVisibleChild = Entity::Null();
		while (child.IsValid() && world.IsAlive(child)) {

			if (drawDescendants || ShouldDrawEntityNode(world, child)) {

				DrawSiblingDropTarget(context, world, child, false);
				DrawEntityNode(context, world, child, drawDescendants);
				lastVisibleChild = child;
			}
			if (!world.HasComponent<HierarchyComponent>(child)) {
				break;
			}
			child = world.GetComponent<HierarchyComponent>(child).nextSibling;
		}
		if (world.IsAlive(lastVisibleChild)) {

			DrawSiblingDropTarget(context, world, lastVisibleChild, true);
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

void Engine::HierarchyPanel::DrawSiblingDropTarget(const EditorPanelContext& context, ECSWorld& world,
	const Entity& anchorEntity, bool insertAfter) {

	if (!context.CanEditScene() || !world.IsAlive(anchorEntity)) {
		return;
	}

	const UUID anchorUUID = world.GetUUID(anchorEntity);
	const std::string id = (insertAfter ? "##SiblingDropAfter" : "##SiblingDropBefore") + ToString(anchorUUID);

	ImGui::PushID(id.c_str());
	const ImVec2 size(ImGui::GetContentRegionAvail().x, 1.0f);
	ImGui::InvisibleButton("##SiblingDropLine", size);

	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyDragDropPayloadType)) {
			if (payload->IsDelivery()) {

				Entity dragged = ResolveDraggedEntity(world, payload);
				if (CanReorder(world, dragged, anchorEntity)) {

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
		ImGui::GetWindowDrawList()->AddLine(
			ImVec2(min.x, (min.y + max.y) * 0.5f),
			ImVec2(max.x, (min.y + max.y) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
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
	if (MyGUI::CollapsingHeader("サブメッシュ", false)) {

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
		if (ImGui::MenuItem("空オブジェクトを作成", nullptr, false, context.CanEditScene())) {

			context.host->ExecuteEditorCommand(std::make_unique<CreateEntityCommand>("Entity"));
		}
		// コピーエンティティを作成
		const bool canPaste = context.editorState && context.editorState->HasClipboard() && context.CanEditScene();
		if (ImGui::MenuItem("コピー済みをペースト", "Ctrl+V", false, canPaste)) {

			context.host->PasteClipboard();
		}
		ImGui::EndPopup();
	}
}

void Engine::HierarchyPanel::DrawRootDropTarget(const EditorPanelContext& context, ECSWorld& world) {

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::TextDisabled("エンティティをルートに戻す");
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

Engine::Entity Engine::HierarchyPanel::GetParentEntity(ECSWorld& world, const Entity& entity) const {

	if (!world.IsAlive(entity) || !world.HasComponent<HierarchyComponent>(entity)) {
		return Entity::Null();
	}
	const auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
	return world.IsAlive(hierarchy.parent) ? hierarchy.parent : Entity::Null();
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

bool Engine::HierarchyPanel::CanReorder(ECSWorld& world, const Entity& child, const Entity& anchor) const {

	if (!world.IsAlive(child) || !world.IsAlive(anchor) || child == anchor) {
		return false;
	}
	return GetParentEntity(world, child) == GetParentEntity(world, anchor);
}

Engine::Entity Engine::HierarchyPanel::ResolveDraggedEntity(ECSWorld& world, const ImGuiPayload* payload) const {

	if (!payload || payload->DataSize != sizeof(UUID)) {
		return Entity::Null();
	}

	// ペイロードからUUIDを取得してエンティティを検索
	const UUID stableUUID = *static_cast<const UUID*>(payload->Data);
	return world.FindByUUID(stableUUID);
}

bool Engine::HierarchyPanel::EntityMatchesSearch(ECSWorld& world, const Entity& entity) const {

	if (!searchFilter_.IsActive()) {
		return true;
	}
	return searchFilter_.Matches(GetEntityDisplayName(world, entity));
}

bool Engine::HierarchyPanel::ShouldDrawEntityNode(ECSWorld& world, const Entity& entity) const {

	if (!searchFilter_.IsActive() || EntityMatchesSearch(world, entity)) {
		return true;
	}
	if (!world.HasComponent<HierarchyComponent>(entity)) {
		return false;
	}

	Entity child = world.GetComponent<HierarchyComponent>(entity).firstChild;
	while (child.IsValid() && world.IsAlive(child)) {

		if (ShouldDrawEntityNode(world, child)) {
			return true;
		}
		if (!world.HasComponent<HierarchyComponent>(child)) {
			break;
		}
		child = world.GetComponent<HierarchyComponent>(child).nextSibling;
	}
	return false;
}