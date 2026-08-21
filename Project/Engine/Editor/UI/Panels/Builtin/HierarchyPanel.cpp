#include "HierarchyPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Commands/Entity/CreateEntityCommand.h>
#include <Engine/Editor/Commands/Entity/DeleteEntityCommand.h>
#include <Engine/Editor/Commands/Entity/ReparentEntityCommand.h>
#include <Engine/Editor/Commands/Entity/DuplicateEntityCommand.h>
#include <Engine/Editor/Commands/Entity/EntityPropertyCommands.h>
#include <Engine/Editor/Commands/Entity/InstantiatePrefabCommand.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentUtility.h>
#include <Engine/Core/Rendering/Meshes/SkeletonBuilder.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Editor/Utility/AssetEntityFactory.h>
#include <Engine/Editor/Utility/PrefabInstanceEditUtility.h>
#include <Engine/Editor/Commands/Entity/CreateDroppedEntityCommand.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <optional>
#include <vector>

//============================================================================
//	HierarchyPanel classMethods
//============================================================================
namespace {

	constexpr const char* kActiveEyeTextureKey = "editor:hierarchy:entityActiveEye";
	constexpr const char* kInactiveEyeTextureKey = "editor:hierarchy:entityActiveOffEye";
	constexpr float kEntityNodeFontScale = 0.88f;

	int32_t GetHierarchySiblingOrder(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return 0;
		}
		return world.GetComponent<Engine::HierarchyComponent>(entity).siblingOrder;
	}

	// プロジェクトからドロップされたアセットをエンティティとして原点に作成する、parentがあればその子にする
	void DropProjectAssetToHierarchy(const Engine::EditorPanelContext& context, Engine::ECSWorld& world,
		const Engine::EditorAssetDragDropPayload& payload, const Engine::Entity& parent) {

		if (!context.CanEditScene() || !context.editorContext || !context.editorContext->assetDatabase || !context.host) {
			return;
		}

		// プレファブは既存のコマンド経路を使い、Undo対応のままインスタンス化する
		if (payload.assetType == Engine::AssetType::Prefab) {

			const Engine::UUID parentUUID = world.IsAlive(parent) ? world.GetUUID(parent) : Engine::UUID{};
			context.host->ExecuteEditorCommand(
				std::make_unique<Engine::InstantiatePrefabCommand>(payload.assetID, parentUUID));
			return;
		}

		// モデル/テクスチャ/フォントはファクトリで生成し、親があればぶら下げる
		if (!Engine::AssetEntityFactory::CanSpawn(payload) || !context.graphicsCore) {
			return;
		}
		Engine::HierarchySystem hierarchySystem{};
		const Engine::AssetSpawnResult spawn = Engine::AssetEntityFactory::Spawn(world,
			*context.editorContext->assetDatabase, *context.graphicsCore, hierarchySystem, payload,
			context.editorContext->activeSceneInstanceID);
		if (!spawn.valid) {
			return;
		}
		if (world.IsAlive(parent)) {
			hierarchySystem.SetParent(world, spawn.root, parent);
		}
		if (context.editorState) {
			context.editorState->SelectEntity(spawn.root);
		}
		// 作成済みエンティティをUndo/Redo対象として履歴へ登録する
		context.host->ExecuteEditorCommand(std::make_unique<Engine::CreateDroppedEntityCommand>(spawn.root));
	}

	// UIプリセット作成メニューを描画する
	void DrawUICreationMenu(const Engine::EditorPanelContext& context, Engine::UUID parentStableUUID) {

		if (!ImGui::BeginMenu("UI", context.CanEditScene())) {
			return;
		}
		auto create = [&](const char* label, const char* name, Engine::EntityCreationPreset preset) {
			if (ImGui::MenuItem(label)) {
				context.host->ExecuteEditorCommand(
					std::make_unique<Engine::CreateEntityCommand>(name, parentStableUUID, preset));
			}
		};
		create("Canvas", "Canvas", Engine::EntityCreationPreset::Canvas);
		ImGui::Separator();
		create("Image", "Image", Engine::EntityCreationPreset::UIImage);
		create("Text", "Text", Engine::EntityCreationPreset::UIText);
		create("Image Button", "Image Button", Engine::EntityCreationPreset::UIImageButton);
		create("Text Button", "Text Button", Engine::EntityCreationPreset::UITextButton);
		create("Progress", "Progress", Engine::EntityCreationPreset::UIProgress);
		ImGui::EndMenu();
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

	const bool visible = ImGui::Begin("Hierarchy", &context.layoutState->showHierarchy);
	if (context.host && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
		context.host->NotifyEditorCommandPanelFocused(
			EditorCommandPanelKind::Scene);
	}
	if (!visible) {
		ImGui::End();
		return;
	}

	RequestActiveIconTextures();

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

	// プレファブ編集中の表示制御、隔離編集は環境を隠し、In-Context編集は編集中のプレファブだけを出す
	const bool prefabEditing = context.editorContext && context.editorContext->isPrefabEditing;
	const bool inContext = context.editorContext && context.editorContext->isPrefabInContext;
	const UUID inContextInstanceID = context.editorContext ?
		context.editorContext->prefabInContextInstanceID : UUID{};
	const std::vector<Entity>* environmentEntities = context.editorContext ? context.editorContext->prefabEnvironmentEntities : nullptr;

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
			if (JointAttachmentUtility::ResolveAttachedJoint(
				*world, entity, skinned, jointSkeletonSpace)) {
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
	visibleEntityRowIndex_ = 0;
	auto drawSceneRoots = [&](UUID sceneInstanceID) {

		Entity lastVisibleRoot = Entity::Null();
		for (const Entity& entity : rootEntities) {

			if (world->GetComponent<SceneObjectComponent>(entity).sceneInstanceID != sceneInstanceID ||
				!ShouldDrawEntityNode(*world, entity)) {
				continue;
			}

			DrawSiblingDropTarget(context, *world, entity, false);
			DrawEntityNode(context, *world, entity, false);
			hasVisibleEntity = true;
			lastVisibleRoot = entity;
		}
		if (world->IsAlive(lastVisibleRoot)) {
			DrawSiblingDropTarget(context, *world, lastVisibleRoot, true);
		}
		};

	SceneInstanceManager* sceneInstances = context.editorContext ?
		context.editorContext->sceneInstances : nullptr;
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
			if (!ShouldDrawEntityNode(*world, entity)) {
				continue;
			}
			DrawSiblingDropTarget(context, *world, entity, false);
			DrawEntityNode(context, *world, entity, false);
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

	if (!world.IsAlive(entity) || !world.HasComponent<SceneObjectComponent>(entity)) {
		return;
	}

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
		hasSubMeshChildren = !GetMeshSubMeshes(world, entity).empty();
	}
	// スキンメッシュのジョイントを持っているか
	bool hasSkinnedMeshChildren = false;
	if (world.HasComponent<SkinnedAnimationComponent>(entity)) {

		const SkinnedAnimationRuntimeData* runtime =
			TryGetSkinnedAnimationRuntime(world, entity);
		hasSkinnedMeshChildren =
			runtime && !runtime->skeleton.joints.empty();
	}

	// ツリー表示できる子がいるか
	bool hasAnyTreeChildren = hasChildren || hasSubMeshChildren || hasSkinnedMeshChildren;

	// ノードのフラグを設定
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_SpanAvailWidth;
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
	if (const NameComponent* name =
		world.TryGetComponent<NameComponent>(entity);
		name && !name->name.empty()) {
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
		ImGui::GetWindowDrawList()->AddRectFilled(
			ImVec2(windowPosition.x + contentMin.x, rowY),
			ImVec2(windowPosition.x + contentMax.x, rowY + ImGui::GetFrameHeight()),
			ImGui::GetColorU32(rowColor));
	}

	const bool additiveSelect = ImGui::IsKeyDown(ImGuiKey_LeftShift);
	auto selectEntityInHierarchy = [&]() {
		if (context.editorContext && context.editorContext->sceneInstances &&
			world.HasComponent<SceneObjectComponent>(entity)) {
			const UUID sceneInstanceID =
				world.GetComponent<SceneObjectComponent>(entity).sceneInstanceID;
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

	// アクティブでない場合はテキストを薄く表示する、プレファブインスタンスは水色で表示する
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
	const float entityNodePaddingY =
		std::max(0.0f, (ImGui::GetFrameHeight() - entityNodeFontHeight) * 0.5f);
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
	// Unity同様ドラッグせず離した時だけ選択し、ドラッグはD&Dとして選択を変えない
	bool nodeLeftClickedNoDrag = ImGui::IsItemHovered() &&
		ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
		!ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left);

	// 左クリックで選択状態にする、右クリックで既に複数選択に含むなら維持する
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
	// TreeNodeExを直前Itemとして扱える位置で、行全体のドラッグ元を登録する
	if (ImGui::BeginDragDropSource()) {

		const UUID stableUUID = world.GetUUID(entity);
		ImGui::SetDragDropPayload(kHierarchyDragDropPayloadType, &stableUUID, sizeof(UUID));
		ImGui::Text("%s", displayName);
		ImGui::EndDragDropSource();
	}

	//============================================================================
	//	ドラッグ目標
	//============================================================================
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyDragDropPayloadType)) {
			if (payload->IsDelivery()) {

				Entity dragged = ResolveDraggedEntity(world, payload);
				if (CanReparent(context, world, dragged, entity)) {

					context.host->ExecuteEditorCommand(
						std::make_unique<ReparentEntityCommand>(dragged, world.GetUUID(entity)));
				}
			}
		}
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectAssetDragDropPayloadType)) {
			if (payload->IsDelivery() && context.CanEditScene() &&
				payload->DataSize == sizeof(EditorAssetDragDropPayload)) {

				// 重なっているエンティティの子としてアセットエンティティを原点に作成する
				const auto* assetPayload = static_cast<const EditorAssetDragDropPayload*>(payload->Data);
				if (assetPayload) {
					DropProjectAssetToHierarchy(context, world, *assetPayload, entity);
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
		DrawUICreationMenu(context, world.GetUUID(entity));
		// エンティティを複製
		if (ImGui::MenuItem("複製", "Ctrl+D", false, context.CanEditScene())) {

			context.host->DuplicateSelection();
		}
		// クリップボードにエンティティをコピー
		if (ImGui::MenuItem("コピー", "Ctrl+C", false, context.CanEditScene())) {

			context.host->CopySelectionToClipboard();
		}
		// エンティティを削除、複数選択ならまとめて消す
		const bool canDelete = PrefabInstanceEditUtility::CanDelete(context.editorContext, world, entity);
		if (ImGui::MenuItem("削除", "Del", false, context.CanEditScene() && canDelete)) {

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
	//	子ノードの表示
	//============================================================================
	if (hasAnyTreeChildren && opened) {

		Entity child = firstChild;
		Entity lastVisibleChild = Entity::Null();
		while (child.IsValid() && world.IsAlive(child)) {

			if (world.HasComponent<SceneObjectComponent>(child) &&
				(drawDescendants || ShouldDrawEntityNode(world, child))) {

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
		// スキンメッシュのジョイント階層の表示
		if (hasSkinnedMeshChildren) {

			ImGui::SetWindowFontScale(0.72f);

			DrawSkinnedMeshNodes(context, world, entity);

			ImGui::SetWindowFontScale(1.0f);
		}
		ImGui::TreePop();
	}

	ImGui::PopID();
	ImGui::PopID();
}

void Engine::HierarchyPanel::DrawSiblingDropTarget(const EditorPanelContext& context, ECSWorld& world,
	const Entity& anchorEntity, bool insertAfter) {

	if (!context.CanEditScene() || !world.IsAlive(anchorEntity)) {
		return;
	}
	const ImGuiPayload* activePayload =
		ImGui::GetDragDropPayload();
	if (!activePayload ||
		!activePayload->IsDataType(
			kHierarchyDragDropPayloadType)) {
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
	const std::span<const SubMeshMaterial> subMeshes =
		GetMeshSubMeshes(world, entity);
	if (subMeshes.empty()) {
		return;
	}

	ImGui::PushID("SubMeshesRoot");
	ImGui::Indent();
	if (MyGUI::CollapsingHeader("サブメッシュ", false)) {

		ImGui::Indent();
		for (uint32_t subMeshIndex = 0;
			subMeshIndex < static_cast<uint32_t>(subMeshes.size()); ++subMeshIndex) {

			const auto& subMesh = subMeshes[subMeshIndex];

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

void Engine::HierarchyPanel::DrawSkinnedMeshNodes(const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity) {

	if (!world.HasComponent<SkinnedAnimationComponent>(entity)) {
		return;
	}
	const SkinnedAnimationRuntimeData* runtime =
		TryGetSkinnedAnimationRuntime(world, entity);
	if (!runtime) {
		return;
	}
	const Skeleton& skeleton = runtime->skeleton;
	if (skeleton.joints.empty()) {
		return;
	}

	// このスキンメッシュへ親子付けされたエンティティをジョイントindexごとに集める
	UUID skinnedLocalFileID{};
	if (world.HasComponent<SceneObjectComponent>(entity)) {
		skinnedLocalFileID = world.GetComponent<SceneObjectComponent>(entity).localFileID;
	}
	std::unordered_map<int32_t, std::vector<Entity>> attachedByJoint;
	if (skinnedLocalFileID) {
		world.ForEachAliveEntity([&](Entity other) {

			if (!world.HasComponent<JointAttachmentComponent>(other)) {
				return;
			}
			Entity attachedSkinned = Entity::Null();
			Matrix4x4 jointSkeletonSpace{};
			if (!JointAttachmentUtility::ResolveAttachedJoint(
				world, other, attachedSkinned, jointSkeletonSpace) || attachedSkinned != entity) {
				return;
			}
			const auto& attachment = world.GetComponent<JointAttachmentComponent>(other);
			const int32_t jointIndex =
				FindSkeletonJointIndex(skeleton, attachment.jointName);
			if (0 <= jointIndex) {
				attachedByJoint[jointIndex].emplace_back(other);
			}
			});
	}

	ImGui::PushID("SkinnedMeshRoot");
	ImGui::Indent();
	if (MyGUI::CollapsingHeader("スキンメッシュ", false)) {

		ImGui::Indent();
		// ルートジョイントから描画する、rootが無効なら親のいないジョイントを全て描く
		if (skeleton.root >= 0 && skeleton.root < static_cast<int32_t>(skeleton.joints.size())) {
			DrawJointNode(context, world, entity, skeleton.root, attachedByJoint);
		} else {
			for (int32_t i = 0; i < static_cast<int32_t>(skeleton.joints.size()); ++i) {
				if (!skeleton.joints[i].parent) {
					DrawJointNode(context, world, entity, i, attachedByJoint);
				}
			}
		}
		ImGui::Unindent();
	}
	ImGui::Unindent();
	ImGui::PopID();
}

void Engine::HierarchyPanel::DrawJointNode(const EditorPanelContext& context, ECSWorld& world,
	const Entity& skinnedEntity, int32_t jointIndex,
	const std::unordered_map<int32_t, std::vector<Entity>>& attachedByJoint) {

	if (!world.HasComponent<SkinnedAnimationComponent>(skinnedEntity)) {
		return;
	}
	const SkinnedAnimationRuntimeData* runtime =
		TryGetSkinnedAnimationRuntime(world, skinnedEntity);
	if (!runtime) {
		return;
	}
	const Skeleton& skeleton = runtime->skeleton;
	if (jointIndex < 0 || jointIndex >= static_cast<int32_t>(skeleton.joints.size())) {
		return;
	}
	const Joint& joint = skeleton.joints[jointIndex];

	ImGui::PushID(jointIndex);

	// 子ジョイントまたは親子付けエンティティを持つか
	auto attachedIt = attachedByJoint.find(jointIndex);
	const bool hasAttached = attachedIt != attachedByJoint.end() && !attachedIt->second.empty();
	const bool hasChildren = !joint.children.empty() || hasAttached;

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (!hasChildren) {
		flags |= ImGuiTreeNodeFlags_Leaf;
	}
	const bool selected = context.editorState && context.editorState->IsJointSelected(skinnedEntity, jointIndex);
	if (selected) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	const std::string label = joint.name.empty() ? ("Joint_" + std::to_string(jointIndex)) : joint.name;
	const bool opened = ImGui::TreeNodeEx("##JointNode", flags, "%s", label.c_str());
	if (ImGui::IsItemClicked() && context.editorState) {
		context.editorState->SelectJoint(skinnedEntity, jointIndex);
	}

	// ドロップ目標、別エンティティをこのジョイントへ親子付けする
	if (ImGui::BeginDragDropTarget()) {

		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyDragDropPayloadType)) {
			if (payload->IsDelivery() && context.CanEditScene()) {

				const Entity dragged = ResolveDraggedEntity(world, payload);
				if (world.IsAlive(dragged) && dragged != skinnedEntity) {

					context.host->ExecuteEditorCommand(
						std::make_unique<ReparentEntityCommand>(dragged, skinnedEntity, joint.name));
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (opened) {

		// 子ジョイントを再帰描画する
		for (int32_t childJoint : joint.children) {
			DrawJointNode(context, world, skinnedEntity, childJoint, attachedByJoint);
		}
		// 親子付けエンティティを実エンティティノードとして表示する
		if (hasAttached) {
			for (const Entity& attached : attachedIt->second) {
				if (world.IsAlive(attached)) {
					DrawEntityNode(context, world, attached, true);
				}
			}
		}
		ImGui::TreePop();
	}
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
		DrawUICreationMenu(context, UUID{});
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

					if (world.HasComponent<JointAttachmentComponent>(dragged)) {

						context.host->ExecuteEditorCommand(
							std::make_unique<ReparentEntityCommand>(dragged, UUID{}));
					} else {

						// ドロップされたエンティティの現在の親を取得
						Entity currentParent = Entity::Null();
						if (world.HasComponent<HierarchyComponent>(dragged)) {
							currentParent = world.GetComponent<HierarchyComponent>(dragged).parent;
						}
						// すでにルートなら何もしない
						if (world.IsAlive(currentParent) &&
							PrefabInstanceEditUtility::CanChangeParent(
								context.editorContext, world, dragged, Entity::Null())) {

							context.host->ExecuteEditorCommand(std::make_unique<ReparentEntityCommand>(dragged, UUID{}));
						}
					}
				}
			}
		}
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectAssetDragDropPayloadType)) {
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

bool Engine::HierarchyPanel::CanReparent(const EditorPanelContext& context, ECSWorld& world,
	const Entity& child, const Entity& newParent) const {

	// どちらも有効なエンティティでなければならない
	if (!world.IsAlive(child) || !world.IsAlive(newParent)) {
		return false;
	}
	if (child == newParent) {
		return false;
	}
	if (!PrefabInstanceEditUtility::CanChangeParent(context.editorContext, world, child, newParent)) {
		return false;
	}
	if (world.HasComponent<SceneObjectComponent>(child) &&
		world.HasComponent<SceneObjectComponent>(newParent) &&
		world.GetComponent<SceneObjectComponent>(child).sceneInstanceID !=
		world.GetComponent<SceneObjectComponent>(newParent).sceneInstanceID) {
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

bool Engine::HierarchyPanel::CanReorder(const EditorPanelContext& context, ECSWorld& world,
	const Entity& child, const Entity& anchor) const {

	if (!world.IsAlive(child) || !world.IsAlive(anchor) || child == anchor) {
		return false;
	}
	if (!PrefabInstanceEditUtility::CanChangeSiblingOrder(context.editorContext, world, child, anchor)) {
		return false;
	}
	if (world.HasComponent<SceneObjectComponent>(child) &&
		world.HasComponent<SceneObjectComponent>(anchor) &&
		world.GetComponent<SceneObjectComponent>(child).sceneInstanceID !=
		world.GetComponent<SceneObjectComponent>(anchor).sceneInstanceID) {
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
