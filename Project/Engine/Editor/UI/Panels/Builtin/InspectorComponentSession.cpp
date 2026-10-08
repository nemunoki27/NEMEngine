#include "InspectorComponentSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Builtin/BuiltinComponentEditorRegistration.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/MeshRendererInspectorDrawer.h>
#include <Engine/Editor/Commands/Components/AddComponentCommand.h>
#include <Engine/Editor/Commands/Components/AddScriptEntryCommand.h>
#include <Engine/Editor/Commands/Components/RemoveComponentCommand.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Scripting/DragDrop/ScriptAssetDragDrop.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>

// c++
#include <string_view>

// imgui
#include <imgui_internal.h>

//============================================================================
//	InspectorComponentSession classMethods
//============================================================================

void Engine::InspectorComponentSession::Init() {

	RegisterBuiltinComponentEditors(componentEditorRegistry_, meshRendererDrawer_);
}

void Engine::InspectorComponentSession::EndPreview() {

	for (const auto& drawer : componentEditorRegistry_.GetDrawers()) {
		drawer->EndPreview();
	}
}

void Engine::InspectorComponentSession::SyncPreviewOwner(ECSWorld* world, Entity entity) {

	for (const auto& drawer : componentEditorRegistry_.GetDrawers()) {
		drawer->SyncPreviewOwner(world, entity);
	}
}

void Engine::InspectorComponentSession::DrawComponents(
	const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	// 対象が持つComponentのDrawerだけを描画する
	for (const auto& drawer : componentEditorRegistry_.GetDrawers()) {
		if (drawer->CanDraw(world, entity)) {
			drawer->Draw(context, world, entity);
		}
	}
}

void Engine::InspectorComponentSession::DrawSubMesh(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	if (meshRendererDrawer_ && meshRendererDrawer_->CanDraw(world, entity)) {
		meshRendererDrawer_->Draw(context, world, entity);
	}
}

void Engine::InspectorComponentSession::DrawComponentToolbar(
	const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	float spacing = ImGui::GetStyle().ItemSpacing.x;
	float width = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;

	const bool canEditRuntimeWorld = context.CanEditScene() || context.IsPlaying();
	if (!canEditRuntimeWorld) {
		ImGui::BeginDisabled();
	}

	// コンポーネントの追加、削除のボタンを表示する
	if (ImGui::Button("コンポーネント追加", ImVec2(width, 0.0f))) {
		addComponentSearchFilter_.Clear();
		addScriptSearchFilter_.Clear();
		ImGui::OpenPopup("##Inspector_AddComponentPopup");
	}
	ImGui::SameLine();
	if (ImGui::Button("コンポーネント削除", ImVec2(width, 0.0f))) {
		ImGui::OpenPopup("##Inspector_RemoveComponentPopup");
	}

	if (!canEditRuntimeWorld) {
		ImGui::EndDisabled();
	}

	// 追加、削除のポップアップを表示する
	DrawAddComponentPopup(context, world, entity);
	DrawRemoveComponentPopup(context, world, entity);

	ImGui::Spacing();
	ImGui::Separator();
}

void Engine::InspectorComponentSession::DrawComponentPopupEntries(const EditorPanelContext& context,
	TextSearchFilter& searchFilter, const char* searchInputID, const char* emptyText,
	const std::function<bool(const ComponentEditorDescriptor&)>& shouldShow,
	const std::function<bool(const ComponentEditorDescriptor&)>& onSelect,
	const std::function<bool(const ComponentEditorDescriptor&)>& drawCustomEntry) {

	// 検索欄の左端にProjectPanelと同じ虫眼鏡アイコンを重ねる
	const ImTextureID searchIcon = EditorTextureHelper::GetSearchIcon(context.graphicsCore->GetTextureUploadService());
	searchFilter.DrawInput(searchInputID, searchIcon, "検索...");
	ImGui::Separator();

	// カテゴリ区切りつきで対象コンポーネントのメニューを表示する
	bool hasAny = false;
	std::string_view currentCategory;
	for (const auto& entry : componentEditorRegistry_.GetDescriptors()) {

		if (!entry.showInComponentMenu) {
			continue;
		}
		// 追加可否や所持状態など対象判定は呼び出し側に委ねる
		if (!shouldShow(entry)) {
			continue;
		}
		if (!searchFilter.Matches(std::string_view(entry.menuLabel)) &&
			!searchFilter.Matches(std::string_view(entry.typeName))) {
			continue;
		}

		const std::string_view entryCategory(entry.category);
		if (currentCategory != entryCategory) {

			if (hasAny) {
				ImGui::Separator();
			}
			currentCategory = entryCategory;
		}
		hasAny = true;
		if (drawCustomEntry && drawCustomEntry(entry)) {
			continue;
		}
		if (ImGui::MenuItem(entry.menuLabel.c_str())) {

			if (onSelect(entry)) {
				ImGui::CloseCurrentPopup();
			}
		}
	}
	// 対象コンポーネントがない
	if (!hasAny) {
		ImGui::TextDisabled(emptyText);
	}
}

void Engine::InspectorComponentSession::DrawAddComponentPopup(
	const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	ImGui::SetNextWindowSize(ImVec2(300.0f, 420.0f), ImGuiCond_Appearing);
	if (!ImGui::BeginPopup("##Inspector_AddComponentPopup")) {
		return;
	}

	DrawComponentPopupEntries(
		context, addComponentSearchFilter_, "##AddComponentSearch", "追加できるコンポーネントはありません",
		// すでに持っているコンポーネントは追加できない、複数追加を許可したものは除く
		[&](const ComponentEditorDescriptor& entry) { return componentEditorRegistry_.CanAdd(entry, world, entity); },
		[&](const ComponentEditorDescriptor& entry) {
			std::unique_ptr<IEditorCommand> command = componentEditorRegistry_.CreateAddCommand(entry, entity);
			if (!command) {
				command = std::make_unique<AddComponentCommand>(entity, entry.typeName);
			}
			context.host->ExecuteEditorCommand(std::move(command));
			return true;
		},
		[&](const ComponentEditorDescriptor& entry) {
			if (entry.typeName != ScriptComponent::kTypeName) {
				return false;
			}

			ImGui::SetNextWindowSize(ImVec2(360.0f, 420.0f), ImGuiCond_Appearing);
			if (ImGui::BeginMenu(entry.menuLabel.c_str())) {
				DrawAddScriptEntries(context, entity);
				ImGui::EndMenu();
			}
			return true;
		});

	ImGui::EndPopup();
}

void Engine::InspectorComponentSession::DrawAddScriptEntries(const EditorPanelContext& context, const Entity& entity) {

	const ImTextureID searchIcon = EditorTextureHelper::GetSearchIcon(context.graphicsCore->GetTextureUploadService());
	addScriptSearchFilter_.DrawInput("##AddScriptSearch", searchIcon, "スクリプト検索...");
	ImGui::Separator();

	const BehaviorTypeRegistry& registry = BehaviorTypeRegistry::GetInstance();
	bool hasAny = false;
	for (uint32_t i = 0; i < registry.GetBehaviorTypeCount(); ++i) {

		const BehaviorTypeInfo& info = registry.GetInfo(i);
		if (!info.managed || info.scriptTypeID.empty() || info.name.empty() || !info.construct) {
			continue;
		}

		const std::string& displayName = info.displayName.empty() ? info.name : info.displayName;
		if (!addScriptSearchFilter_.Matches(displayName) && !addScriptSearchFilter_.Matches(info.name)) {
			continue;
		}

		hasAny = true;
		ImGui::PushID(static_cast<int32_t>(i));
		if (ImGui::Selectable(displayName.c_str())) {

			AssetID scriptAsset{};
			if (context.editorContext && context.editorContext->assetDatabase) {
				if (const AssetMeta* meta = context.editorContext->assetDatabase->FindByPath(info.sourcePath)) {
					scriptAsset = meta->guid;
				}
			}
			context.host->ExecuteEditorCommand(
				std::make_unique<AddScriptEntryCommand>(entity, info.scriptTypeID, info.name, scriptAsset));
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::IsItemHovered() && displayName != info.name) {
			ImGui::SetTooltip("%s", info.name.c_str());
		}
		ImGui::PopID();
	}
	if (!hasAny) {
		ImGui::TextDisabled("一致するスクリプトはありません");
	}
}

void Engine::InspectorComponentSession::DrawScriptAssetDropTarget(const EditorPanelContext& context, const Entity& entity) {

	if ((!context.CanEditScene() && !context.IsPlaying()) || !context.host) {
		return;
	}

	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (!window || !ImGui::BeginDragDropTargetCustom(window->InnerRect, window->GetID("##InspectorScriptDropTarget"))) {
		return;
	}

	AssetID scriptAsset{};
	ScriptAssetDragDrop::ResolvedScriptType resolved{};
	if (ScriptAssetDragDrop::AcceptScriptAssetDrop(context, scriptAsset, resolved)) {
		context.host->ExecuteEditorCommand(
			std::make_unique<AddScriptEntryCommand>(entity, resolved.scriptTypeID, resolved.typeName, scriptAsset));
	}
	ImGui::EndDragDropTarget();
}

void Engine::InspectorComponentSession::DrawRemoveComponentPopup(
	const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	if (!ImGui::BeginPopup("##Inspector_RemoveComponentPopup")) {
		return;
	}

	DrawComponentPopupEntries(
		context, removeComponentSearchFilter_, "##RemoveComponentSearch", "No removable components.",
		// 持っていないコンポーネントは削除できない
		[&](const ComponentEditorDescriptor& entry) {
			return entry.showInRemoveMenu && world.HasComponent(entity, entry.typeName);
		},
		[&](const ComponentEditorDescriptor& entry) {
			context.host->ExecuteEditorCommand(std::make_unique<RemoveComponentCommand>(entity, entry.typeName));
			return true;
		});

	ImGui::EndPopup();
}
