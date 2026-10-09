#include "InspectorPanel.h"
#include "InspectorSelectionDisplay.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/EntityPropertyCommands.h>
#include <Engine/Editor/Settings/ProjectTagSettings.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Tools/Registry/ToolRegistry.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Asset/TextureAssetInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Asset/MeshAssetInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Asset/RenderTextureAssetInspectorDrawer.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <memory>
#include <string>
#include <vector>

//============================================================================
//	InspectorPanel classMethods
//============================================================================

Engine::InspectorPanel::InspectorPanel(const std::string& instanceID, bool primaryInstance) {

	ConfigureInstance("Inspector", instanceID, primaryInstance);

	componentSession_.Init();

	// アセット種別ごとのInspector表示を登録する
	assetInspectorRegistry_.Register(std::make_unique<TextureAssetInspectorDrawer>());
	assetInspectorRegistry_.Register(std::make_unique<MeshAssetInspectorDrawer>());
	assetInspectorRegistry_.Register(std::make_unique<RenderTextureAssetInspectorDrawer>());
}

nlohmann::json Engine::InspectorPanel::SaveLayoutState() const {

	return {
		{"mode", lockedEntityUUID_ ? "LockedEntity" : "FollowSelection"},
		{"lockedEntityUUID", lockedEntityUUID_ ? ToString(lockedEntityUUID_) : std::string{}},
	};
}

void Engine::InspectorPanel::LoadLayoutState(const nlohmann::json& state) {

	lockedEntityUUID_ = {};
	if (!state.is_object() || state.value("mode", std::string{}) != "LockedEntity") {
		return;
	}
	lockedEntityUUID_ = FromString16Hex(state.value("lockedEntityUUID", std::string{}));
}

nlohmann::json Engine::InspectorPanel::MakeDuplicateState(const EditorPanelContext& context) const {

	UUID targetUUID = lockedEntityUUID_;
	ECSWorld* world = context.GetWorld();
	const bool entitySelection =
		context.editorState && (context.editorState->selectionKind == EditorSelectionKind::Entity ||
								   context.editorState->selectionKind == EditorSelectionKind::MeshSubMesh);
	if (!targetUUID && world && entitySelection && context.editorState->HasValidSelection(world)) {
		targetUUID = world->GetUUID(context.editorState->selectedEntity);
	}

	return {
		{"mode", "LockedEntity"},
		{"lockedEntityUUID", targetUUID ? ToString(targetUUID) : std::string{}},
	};
}

bool Engine::InspectorPanel::CanDuplicate(const EditorPanelContext& context) const {

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}
	if (lockedEntityUUID_) {
		return world->IsAlive(world->FindByUUID(lockedEntityUUID_));
	}
	if (!context.editorState || (context.editorState->selectionKind != EditorSelectionKind::Entity &&
									context.editorState->selectionKind != EditorSelectionKind::MeshSubMesh)) {
		return false;
	}
	return context.editorState->HasValidSelection(world);
}

void Engine::InspectorPanel::EndPreview() {

	componentSession_.EndPreview();
}

void Engine::InspectorPanel::Draw(const EditorPanelContext& context) {

	// インスペクターパネルの表示状態を確認
	bool* open = ResolveOpenState(&context.layoutState->showInspector);
	ECSWorld* previewWorld = context.GetWorld();
	Entity previewEntity = Entity::Null();
	if (*open && previewWorld) {
		if (lockedEntityUUID_) {
			previewEntity = previewWorld->FindByUUID(lockedEntityUUID_);
		} else if (context.editorState->selectionKind == EditorSelectionKind::Entity &&
				   context.editorState->HasValidSelection(previewWorld)) {
			previewEntity = context.editorState->selectedEntity;
		}
	}
	componentSession_.SyncPreviewOwner(previewWorld, previewEntity);
	if (!*open) {
		EndPreview();
	}
	assetEditSession_.KeepPanelOpen(*open);
	if (!*open) {
		prefabSession_.Clear();
		return;
	}

	ECSWorld* world = context.GetWorld();
	Entity lockedEntity = Entity::Null();
	std::string displayName = "Inspector";
	if (lockedEntityUUID_) {

		lockedEntity = world ? world->FindByUUID(lockedEntityUUID_) : Entity::Null();
		displayName = world && world->IsAlive(lockedEntity) ? "Inspector: " + GetEntityDisplayName(*world, lockedEntity)
															: "Inspector: Missing Entity";
	}

	const std::string windowName = MakeWindowName(displayName);
	ApplyInitialDock();
	const bool visible = ImGui::Begin(windowName.c_str(), open);
	assetEditSession_.KeepPanelOpen(*open);
	if (assetEditSession_.ResolvePanelClose(context, *open)) {
		ImGui::End();
		return;
	}
	if (context.host && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
		context.host->NotifyEditorCommandPanelFocused(EditorCommandPanelKind::Scene);
	}
	if (!visible) {
		DrawTitleBarContextMenu(context);
		ImGui::End();
		return;
	}
	DrawTitleBarContextMenu(context);

	if (!lockedEntityUUID_ && context.editorState->selectionKind == EditorSelectionKind::Asset) {

		ImGui::SetWindowFontScale(fontScale_);
		DrawSelectedAssetInspector(context);
		ImGui::SetWindowFontScale(1.0f);
		ImGui::End();
		return;
	}
	// スキンメッシュのジョイント選択時は専用のインスペクターを出す
	if (!lockedEntityUUID_ && context.editorState->selectionKind == EditorSelectionKind::Joint) {

		ImGui::SetWindowFontScale(fontScale_);
		InspectorSelectionDisplay::DrawJointInspector(context);
		ImGui::SetWindowFontScale(1.0f);
		ImGui::End();
		return;
	}

	if (lockedEntityUUID_ && (!world || !world->IsAlive(lockedEntity))) {

		ImGui::TextDisabled("固定したEntityが見つかりません");
		ImGui::End();
		return;
	}
	if (!lockedEntityUUID_ && !context.editorState->HasValidSelection(world)) {

		ImGui::TextDisabled("エンティティが選択されていません");
		ImGui::End();
		return;
	}

	// 選択されているエンティティを取得
	Entity selected = lockedEntityUUID_ ? lockedEntity : context.editorState->selectedEntity;

	ImGui::SetWindowFontScale(fontScale_);

	// サブメッシュが選択されている場合はサブメッシュのインスペクターを表示
	if (!lockedEntityUUID_ && context.editorState->HasValidSubMeshSelection(world)) {

		InspectorSelectionDisplay::DrawSelectedSubMeshHeader(context, *world, selected);
		componentSession_.DrawSubMesh(context, *world, selected);
		ImGui::SetWindowFontScale(1.0f);
		ImGui::End();
		return;
	}

	// エンティティのヘッダー部分を描画
	DrawEntityHeader(context, *world, selected);
	// コンポーネント操作UI
	componentSession_.DrawComponentToolbar(context, *world, selected);

	// プレファブインスタンスならオーバーライド一覧UIを描画する
	prefabSession_.Draw(context, *world, selected);

	// Entity情報と操作ボタンを固定し、コンポーネント一覧だけを残り領域でスクロールする
	const bool componentsVisible = ImGui::BeginChild("##InspectorComponents", ImVec2(0.0f, 0.0f), true);
	// Childは別Windowとして扱われるため、Inspectorの文字倍率を明示的に引き継ぐ
	ImGui::SetWindowFontScale(fontScale_);
	if (componentsVisible) {

		// D&D中もコンポーネント一覧上のホイール操作を受け付ける
		if (ImGui::GetDragDropPayload() != nullptr && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {

			const float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f) {
				ImGui::SetScrollY(ImGui::GetScrollY() - wheel * ImGui::GetFontSize() * 3.0f);
			}
		}

		componentSession_.DrawComponents(context, *world, selected);
	}
	ImGui::EndChild();
	componentSession_.DrawScriptAssetDropTarget(context, selected);

	ImGui::SetWindowFontScale(1.0f);

	ImGui::End();
}

void Engine::InspectorPanel::SyncNameBufferIfNeeded(ECSWorld& world, const Entity& entity) {

	// 同じEntityの編集中はバッファを保持する
	const UUID stableUUID = world.GetUUID(entity);
	if (editingNameEntityStableUUID_ == stableUUID) {
		return;
	}

	// 名前編集対象のエンティティが変わったので、バッファを同期する
	editingNameEntityStableUUID_ = stableUUID;
	if (world.HasComponent<NameComponent>(entity)) {

		nameEditBuffer_ = world.GetComponent<NameComponent>(entity).name;
	} else {

		nameEditBuffer_ = "Entity";
	}
}

void Engine::InspectorPanel::DrawEntityHeader(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	// エンティティのハンドル情報を表示
	ImGui::Text("Entity Handle : [%u:%u]", entity.index, entity.generation);

	//============================================================================
	//	エンティティの名前編集
	//============================================================================
	// 現在の名前を取得する
	std::string currentName = "Entity";
	if (world.HasComponent<NameComponent>(entity)) {
		currentName = world.GetComponent<NameComponent>(entity).name;
	}

	// 編集対象のエンティティが変わったらバッファを同期する
	UUID stableUUID = world.GetUUID(entity);
	if (editingNameEntityStableUUID_ != stableUUID) {
		SyncNameBufferIfNeeded(world, entity);
	}

	// 名前の入力欄を表示する
	auto editResult = MyGUI::InputText("Name", nameEditBuffer_);
	if (editResult.editFinished) {
		if (nameEditBuffer_ != currentName) {
			if (context.IsPlaying()) {
				// Play中は実行Worldの名前だけを書き換える
				if (auto* name = world.TryGetComponent<NameComponent>(entity)) {
					name->name = nameEditBuffer_;
					world.MarkComponentModified<NameComponent>(entity);
				}
			} else {
				context.host->ExecuteEditorCommand(std::make_unique<RenameEntityCommand>(entity, nameEditBuffer_));
			}
		}
	}

	// アクティブ中は同期
	if (!editResult.anyItemActive) {
		SyncNameBufferIfNeeded(world, entity);
	}

	//============================================================================
	//	タグ、固定リストから選ぶ
	//============================================================================
	std::string currentTag = "Untagged";
	if (world.HasComponent<SceneObjectComponent>(entity)) {
		currentTag = world.GetComponent<SceneObjectComponent>(entity).tag;
	}

	const std::vector<std::string>& tags = context.tagSettings->GetTags();
	std::string editTag = currentTag;
	auto tagResult = MyGUI::StringCombo("Tag", editTag, std::span<const std::string>(tags.data(), tags.size()));
	if (tagResult.valueChanged && editTag != currentTag) {
		if (context.IsPlaying()) {
			// Play中は実行Worldのタグだけを書き換える
			if (auto* sceneObject = world.TryGetComponent<SceneObjectComponent>(entity)) {
				sceneObject->tag = editTag;
				world.MarkComponentModified<SceneObjectComponent>(entity);
			}
		} else {
			context.host->ExecuteEditorCommand(std::make_unique<SetEntityTagCommand>(entity, editTag));
		}
	}

	// タグ管理ツールを開く
	ImGui::SameLine();
	if (ImGui::SmallButton("...##OpenTagManager")) {
		if (ITool* tool = ToolRegistry::GetInstance().Find("engine.tag_manager")) {
			if (auto* tagManager = dynamic_cast<IEditorTool*>(tool)) {
				tagManager->OpenEditorTool();
			}
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
}

void Engine::InspectorPanel::DrawSelectedAssetInspector(const EditorPanelContext& context) {

	if (!context.editorContext || !context.editorContext->assetDatabase || !context.editorState->selectedAsset) {
		ImGui::TextDisabled("Asset is not selected.");
		return;
	}

	const AssetDatabase* database = context.editorContext->assetDatabase;
	const AssetMeta* meta = database->Find(context.editorState->selectedAsset);
	if (!meta) {

		ImGui::TextDisabled("Selected asset was not found.");
		return;
	}

	ImGui::Text("Asset");
	ImGui::Separator();
	MyGUI::Text("Path", meta->assetPath);
	MyGUI::Text("Type", EnumAdapter<AssetType>::ToString(meta->type));
	MyGUI::Text("ID", ToString(meta->guid));
	ImGui::Spacing();

	// Asset種別の編集Drawerへ渡す
	if (IAssetInspectorDrawer* drawer = assetInspectorRegistry_.Find(meta->type)) {

		assetEditSession_.TrackDrawer(drawer, meta->guid);
		drawer->Draw(context, *meta);
		if (meta->type == AssetType::Mesh) {
			// インポート設定とモデルプレビューを同時に表示する
			modelPreview_.DrawMeshAssetInspector(context, *meta);
		}
		return;
	}
	assetEditSession_.TrackDrawer(nullptr, {});

	// Materialの専用編集へ渡す
	if (meta->type == AssetType::Material) {

		materialSession_.DrawMaterialAssetInspector(context, *meta);
		return;
	}
	if (meta->type == AssetType::Mesh) {

		modelPreview_.DrawMeshAssetInspector(context, *meta);
		return;
	}

	ImGui::TextDisabled("この種類のアセットには詳細編集がありません");
}

bool Engine::InspectorPanel::HasPendingEdits() const {

	return assetEditSession_.HasPendingEdits();
}

void Engine::InspectorPanel::RequestResolvePendingEdits() {

	assetEditSession_.RequestResolvePendingEdits();
}

Engine::EditorPanelCloseResult Engine::InspectorPanel::ConsumePendingEditCloseResult() {

	return assetEditSession_.ConsumePendingEditCloseResult();
}
