#include "HierarchyEntityTree.h"
#include "HierarchyEntityOperations.h"
#include "HierarchyMeshTree.h"
#include "HierarchyDropTargets.h"
#include "HierarchyEntityMenu.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Commands/Entity/ReparentEntitiesCommand.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentUtility.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <optional>
#include <vector>

using namespace Engine::HierarchyEntityOperations;

namespace {

	constexpr const char* kActiveEyeTextureKey = "editor:hierarchy:entityActiveEye";
	constexpr const char* kInactiveEyeTextureKey = "editor:hierarchy:entityActiveOffEye";
	constexpr float kEntityNodeFontScale = 0.88f;
}

Engine::HierarchyEntityTree::HierarchyEntityTree(TextureUploadService& textureUploadService)
	: textureUploadService_(textureUploadService) {
}

void Engine::HierarchyEntityTree::DrawSearch() {

	// 表示アイコンを要求して検索欄を描画
	RequestActiveIconTextures();
	const ImTextureID searchIcon = EditorTextureHelper::GetSearchIcon(textureUploadService_);
	searchFilter_.DrawInput("##HierarchySearch", searchIcon, "検索...");
}

void Engine::HierarchyEntityTree::BeginFrame() {

	// 行の交互表示を先頭から開始
	visibleEntityRowIndex_ = 0;
}

void Engine::HierarchyEntityTree::RequestActiveIconTextures() {

	if (activeIconRequested_) {
		return;
	}

	textureUploadService_.RequestTextureFile(
		kActiveEyeTextureKey, EditorTextureHelper::MakeEditorTexturePath("Hierarchy", "entityActiveEye.dds"));
	textureUploadService_.RequestTextureFile(
		kInactiveEyeTextureKey, EditorTextureHelper::MakeEditorTexturePath("Hierarchy", "entityActiveOffEye.dds"));
	activeIconRequested_ = true;
}

void Engine::HierarchyEntityTree::DrawActiveToggleIcon(const EditorPanelContext& context, ECSWorld& world, const Entity& entity,
	bool activeSelf, bool& leftClicked, bool& rightClicked) {

	leftClicked = false;
	rightClicked = false;

	// アクティブ状態に対応するアイコンを取得
	const char* textureKey = activeSelf ? kActiveEyeTextureKey : kInactiveEyeTextureKey;
	const ImTextureID textureID = EditorTextureHelper::GetImTextureID(textureUploadService_, textureKey);

	const float iconSize = ImGui::GetTextLineHeight() * 0.92f;
	const ImVec2 buttonSize(iconSize, iconSize);

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

	if (toggled && world.IsAlive(entity)) {

		// 選択中のエンティティなら全選択へ同じ状態を適用する
		const bool newActive = !activeSelf;
		SetEntityActiveFromHierarchy(context, world, entity, newActive);
	}
}

void Engine::HierarchyEntityTree::DrawEntityNode(
	const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool forceVisible) {

	if (!world.IsAlive(entity) || !world.HasComponent<SceneObjectComponent>(entity)) {
		return;
	}

	const bool selfMatchesSearch = EntityMatchesSearch(world, entity);
	const bool drawDescendants = forceVisible || selfMatchesSearch;

	// アクティブ状態を取得
	const auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
	const bool activeSelf = sceneObject.activeSelf;
	const bool activeInHierarchy = sceneObject.activeInHierarchy;

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
		hasSubMeshChildren = !GetMeshSubMeshes(world, entity).empty();
	}
	// スキンメッシュのジョイントを持っているか
	bool hasSkinnedMeshChildren = false;
	if (world.HasComponent<SkinnedAnimationComponent>(entity)) {

		const SkinnedAnimationRuntimeData* runtime = TryGetSkinnedAnimationRuntime(world, entity);
		hasSkinnedMeshChildren = runtime && !runtime->skeleton.joints.empty();
	}

	// ツリー表示できる子がいるか
	bool hasAnyTreeChildren = hasChildren || hasSubMeshChildren || hasSkinnedMeshChildren;

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

	// 表示名はNameComponentの文字列を直接参照して行ごとの確保を避ける
	const char* displayName = "Entity";
	if (const NameComponent* name = world.TryGetComponent<NameComponent>(entity); name && !name->name.empty()) {
		displayName = name->name.c_str();
	}

	ImGui::PushID(static_cast<int>(entity.index));
	ImGui::PushID(static_cast<int>(entity.generation));

	// 行を交互に塗り、深い階層でも横方向を追いやすくする
	if ((visibleEntityRowIndex_++ & 1u) != 0u) {

		ImVec4 rowColor = ImGui::GetStyleColorVec4(ImGuiCol_Header);
		rowColor.w = 0.10f;
		const ImVec2 windowPosition = ImGui::GetWindowPos();
		const ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
		const ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
		const float rowY = ImGui::GetCursorScreenPos().y;
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(windowPosition.x + contentMin.x, rowY),
			ImVec2(windowPosition.x + contentMax.x, rowY + ImGui::GetFrameHeight()), ImGui::GetColorU32(rowColor));
	}

	const bool additiveSelect = ImGui::IsKeyDown(ImGuiKey_LeftShift);
	auto selectEntityInHierarchy = [&]() {
		if (context.editorContext && context.editorContext->sceneInstances &&
			world.HasComponent<SceneObjectComponent>(entity)) {
			const UUID sceneInstanceID = world.GetComponent<SceneObjectComponent>(entity).sceneInstanceID;
			if (sceneInstanceID) {
				context.editorContext->sceneInstances->SetActive(sceneInstanceID);
			}
		}
		if (additiveSelect && context.editorState->selectKind == EditorSelectionKind::Entity &&
			context.editorState->CanMultiSelect(world, entity)) {
			context.editorState->ToggleEntityInSelection(entity);
		} else {
			context.editorState->SelectEntity(entity);
		}
	};

	//============================================================================
	//	ツリーノード本体
	//============================================================================

	// 非アクティブとPrefabを文字色で区別
	const bool isPrefabInstance = world.HasComponent<PrefabLinkComponent>(entity);
	bool isBrokenPrefab = false;
	if (isPrefabInstance && context.editorContext && context.editorContext->assetDatabase) {

		const AssetID prefabAsset = world.GetComponent<PrefabLinkComponent>(entity).prefabAsset;
		isBrokenPrefab = !prefabAsset || !context.editorContext->assetDatabase->Find(prefabAsset);
	}
	bool pushedTextColor = false;
	if (isBrokenPrefab) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
		pushedTextColor = true;
	} else if (!activeInHierarchy) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		pushedTextColor = true;
	} else if (isPrefabInstance) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.80f, 1.0f, 1.0f));
		pushedTextColor = true;
	}

	const ImGuiStyle& style = ImGui::GetStyle();
	const float entityNodeFontHeight = ImGui::GetFontSize() * kEntityNodeFontScale;
	const float entityNodePaddingY = std::max(0.0f, (ImGui::GetFrameHeight() - entityNodeFontHeight) * 0.5f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, entityNodePaddingY));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 1.0f));
	ImGui::SetWindowFontScale(kEntityNodeFontScale);
	ImGui::SetNextItemAllowOverlap();
	bool opened = ImGui::TreeNodeEx("##HierarchyNode", flags, "%s", displayName);
	ImGui::SetWindowFontScale(1.0f);
	ImGui::PopStyleVar(2);

	if (pushedTextColor) {
		ImGui::PopStyleColor();
	}

	// 右クリックは押した時に判定する
	bool nodeRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
	// ドラッグせずボタンを離した時だけ選択
	bool nodeLeftClickedNoDrag = ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
								 !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left);

	// 右クリックでは既存の複数選択を維持
	if (nodeLeftClickedNoDrag) {
		selectEntityInHierarchy();
	} else if (nodeRightClicked && !context.editorState->IsEntitySelected(entity)) {
		context.editorState->SelectEntity(entity);
	}

	// ダブルクリックでシーンカメラをそのエンティティへ寄せる
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		const std::optional<Dimension> dimension = ResolveEntityDimension(world, entity);
		if (dimension && *dimension == Dimension::Type3D) {
			context.editorState->cameraFocusRequest = entity;
		}
	}

	// ノード右クリックでもコンテキストメニューを開く
	if (nodeRightClicked) {
		ImGui::OpenPopup("HierarchyEntityContextMenu");
	}

	//============================================================================
	//	ドラッグ開始
	//============================================================================
	// ツリーの行をドラッグ元に登録
	if (ImGui::BeginDragDropSource()) {

		const UUID stableUUID = world.GetUUID(entity);
		ImGui::SetDragDropPayload(IEditorPanel::kHierarchyDragDropPayloadType, &stableUUID, sizeof(UUID));
		ImGui::Text("%s", displayName);
		ImGui::EndDragDropSource();
	}

	//============================================================================
	//	ドラッグ目標
	//============================================================================
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kHierarchyDragDropPayloadType)) {
			if (payload->IsDelivery()) {

				std::vector<Entity> draggedEntities = ResolveDraggedEntities(context, world, payload);
				draggedEntities.erase(std::remove_if(draggedEntities.begin(), draggedEntities.end(),
										  [&](const Entity& dragged) { return !CanReparent(context, world, dragged, entity); }),
					draggedEntities.end());
				if (!draggedEntities.empty()) {
					context.host->ExecuteEditorCommand(
						std::make_unique<ReparentEntitiesCommand>(std::move(draggedEntities), world.GetUUID(entity)));
				}
			}
		}
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kProjectAssetDragDropPayloadType)) {
			if (payload->IsDelivery() && context.CanEditScene() && payload->DataSize == sizeof(EditorAssetDragDropPayload)) {

				// 重なっているエンティティの子としてアセットエンティティを原点に作成する
				const auto* assetPayload = static_cast<const EditorAssetDragDropPayload*>(payload->Data);
				if (assetPayload) {
					HierarchyDropTargets::DropProjectAssetToHierarchy(context, world, *assetPayload, entity);
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	// Blenderと同じく、アクティブ切り替えを行の右端へ固定する
	const float iconSize = ImGui::GetTextLineHeight() * 0.92f;
	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - iconSize);
	bool checkboxLeftClicked = false;
	bool checkboxRightClicked = false;
	DrawActiveToggleIcon(context, world, entity, activeSelf, checkboxLeftClicked, checkboxRightClicked);
	if (checkboxLeftClicked || checkboxRightClicked) {
		selectEntityInHierarchy();
	}
	if (checkboxRightClicked) {
		ImGui::OpenPopup("HierarchyEntityContextMenu");
	}

	//============================================================================
	//	右クリックのコンテキストメニュー
	//============================================================================
	HierarchyEntityMenu::Draw(context, world, entity, activeSelf);

	//============================================================================
	//	子ノードの表示
	//============================================================================
	if (hasAnyTreeChildren && opened) {

		Entity child = firstChild;
		Entity lastVisibleChild = Entity::Null();
		while (child.IsValid() && world.IsAlive(child)) {

			if (world.HasComponent<SceneObjectComponent>(child) && (drawDescendants || ShouldDrawEntityNode(world, child))) {

				HierarchyDropTargets::DrawSiblingDropTarget(context, world, child, false);
				DrawEntityNode(context, world, child, drawDescendants);
				lastVisibleChild = child;
			}
			if (!world.HasComponent<HierarchyComponent>(child)) {
				break;
			}
			child = world.GetComponent<HierarchyComponent>(child).nextSibling;
		}
		if (world.IsAlive(lastVisibleChild)) {

			HierarchyDropTargets::DrawSiblingDropTarget(context, world, lastVisibleChild, true);
		}

		// サブメッシュノードの表示
		if (hasSubMeshChildren) {

			ImGui::SetWindowFontScale(0.72f);

			HierarchyMeshTree::DrawSubMeshNodes(context, world, entity);

			ImGui::SetWindowFontScale(1.0f);
		}
		// スキンメッシュのジョイント階層の表示
		if (hasSkinnedMeshChildren) {

			ImGui::SetWindowFontScale(0.72f);

			HierarchyMeshTree::DrawSkinnedMeshNodes(
				context, world, entity, [&](const Entity& attached) { DrawEntityNode(context, world, attached, true); });

			ImGui::SetWindowFontScale(1.0f);
		}
		ImGui::TreePop();
	}

	ImGui::PopID();
	ImGui::PopID();
}

bool Engine::HierarchyEntityTree::EntityMatchesSearch(ECSWorld& world, const Entity& entity) const {

	if (!searchFilter_.IsActive()) {
		return true;
	}
	return searchFilter_.Matches(GetEntityDisplayName(world, entity));
}

bool Engine::HierarchyEntityTree::ShouldDrawEntityNode(ECSWorld& world, const Entity& entity) const {

	if (!world.IsAlive(entity) || !world.HasComponent<SceneObjectComponent>(entity)) {
		return false;
	}
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
