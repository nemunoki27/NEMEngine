#include "InspectorPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/Methods/RenameEntityCommand.h>
#include <Engine/Editor/Command/Methods/AddComponentCommand.h>
#include <Engine/Editor/Command/Methods/AddScriptEntryCommand.h>
#include <Engine/Editor/Command/Methods/RemoveComponentCommand.h>
#include <Engine/Editor/Panel/Interface/IEditorPanelHost.h>
#include <Engine/Editor/Scripting/ScriptAssetDragDrop.h>
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Core/ECS/Component/Builtin/ScriptComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/SpriteRendererComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/TextRendererComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/ECS/Component/Builtin/CameraComponent.h>
#include <Engine/Editor/Command/Methods/SetEntityActiveCommand.h>
#include <Engine/Editor/Inspector/Methods/Common/InspectorDrawerCommon.h>
#include <Engine/Utility/ImGui/MyGUI.h>

// インスペクター描画
#include <Engine/Editor/Inspector/Methods/TransformInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/UVTransformInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/ScriptInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/CameraInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/Render/SpriteRendererInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/Render/MeshRendererInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/Render/TextRendererInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/Light/DirectionalLightInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/Light/PointLightInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/Light/SpotLightInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/Animation/SkinnedAnimationInspectorDrawer.h>

// c++
#include <string_view>

//============================================================================
//	InspectorPanel classMethods
//============================================================================

namespace {

	// インスペクターパネルのコンポーネント追加メニューのエントリー
	struct InspectorComponentMenuEntry {
		const char* menuLabel;
		const char* typeName;
	};
	// 追加できるコンポーネントのメニューエントリー
	constexpr std::array<InspectorComponentMenuEntry, 11> kOptionalComponentMenuEntries = { {

		{ "PerspectiveCamera",  "PerspectiveCamera" },
		{ "OrthographicCamera", "OrthographicCamera" },
		{ "Script",             "Script" },
		{ "Mesh Renderer",      "MeshRenderer" },
		{ "Skinned Animation",  "SkinnedAnimation" },
		{ "Sprite Renderer",    "SpriteRenderer" },
		{ "Text Renderer",      "TextRenderer" },
		{ "UVTransform",      "UVTransform" },
		{ "DirectionalLight", "DirectionalLight" },
		{ "PointLight",       "PointLight" },
		{ "SpotLight",        "SpotLight" },
	} };

	// Scriptメニューか
	bool IsScriptMenuEntry(const InspectorComponentMenuEntry& entry) {

		return std::string_view(entry.typeName) == "Script";
	}

	// Inspector全体でScriptアセットのドロップを受け取る
	void DrawScriptAssetDropTarget(const Engine::EditorPanelContext& context, const Engine::Entity& entity) {

		if (!context.CanEditScene()) {
			return;
		}

		ImGui::Button("Drop C# Script", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
		if (!ImGui::BeginDragDropTarget()) {
			return;
		}

		Engine::AssetID scriptAsset{};
		std::string typeName{};
		if (Engine::ScriptAssetDragDrop::AcceptScriptAssetDrop(context, scriptAsset, typeName)) {

			context.host->ExecuteEditorCommand(std::make_unique<Engine::AddScriptEntryCommand>(
				entity, typeName, scriptAsset));
		}
		ImGui::EndDragDropTarget();
	}
}

Engine::InspectorPanel::InspectorPanel() {

	// コンポーネント描画の登録
	componentDrawers_.emplace_back(std::make_unique<TransformInspectorDrawer>());
	componentDrawers_.emplace_back(std::make_unique<OrthographicCameraInspectorDrawer>());
	componentDrawers_.emplace_back(std::make_unique<PerspectiveCameraInspectorDrawer>());
	componentDrawers_.emplace_back(std::make_unique<SpriteRendererInspectorDrawer>());
	{
		auto meshDrawer = std::make_unique<MeshRendererInspectorDrawer>();
		meshRendererDrawer_ = meshDrawer.get();
		componentDrawers_.emplace_back(std::move(meshDrawer));
	}
	componentDrawers_.emplace_back(std::make_unique<SkinnedAnimationInspectorDrawer>());
	componentDrawers_.emplace_back(std::make_unique<TextRendererInspectorDrawer>());
	componentDrawers_.emplace_back(std::make_unique<UVTransformInspectorDrawer>());
	componentDrawers_.emplace_back(std::make_unique<DirectionalLightInspectorDrawer>());
	componentDrawers_.emplace_back(std::make_unique<PointLightInspectorDrawer>());
	componentDrawers_.emplace_back(std::make_unique<SpotLightInspectorDrawer>());
	componentDrawers_.emplace_back(std::make_unique<ScriptInspectorDrawer>());
}

void Engine::InspectorPanel::Draw(const EditorPanelContext& context) {

	// インスペクターパネルの表示状態を確認
	if (!context.layoutState->showInspector) {
		return;
	}

	if (!ImGui::Begin("Inspector", &context.layoutState->showInspector)) {
		ImGui::End();
		return;
	}

	ECSWorld* world = context.GetWorld();
	if (!context.editorState->HasValidSelection(world)) {

		ImGui::TextDisabled("Entity is not selected.");
		ImGui::End();
		return;
	}

	// 選択されているエンティティを取得
	Entity selected = context.editorState->selectedEntity;

	ImGui::SetWindowFontScale(fontScale_);

	// サブメッシュが選択されている場合はサブメッシュのインスペクターを表示
	if (context.editorState->HasValidSubMeshSelection(world)) {

		DrawSelectedSubMeshHeader(context, *world, selected);
		if (meshRendererDrawer_ && meshRendererDrawer_->CanDraw(*world, selected)) {

			meshRendererDrawer_->Draw(context, *world, selected);
		}
		ImGui::SetWindowFontScale(1.0f);
		ImGui::End();
		return;
	}

	// エンティティのヘッダー部分を描画
	DrawEntityHeader(context, *world, selected);
	// コンポーネント操作UI
	DrawComponentToolbar(context, *world, selected);
	DrawScriptAssetDropTarget(context, selected);

	// 登録済みコンポーネント描画
	for (const auto& drawer : componentDrawers_) {

		if (!drawer->CanDraw(*world, selected)) {
			continue;
		}
		drawer->Draw(context, *world, selected);
	}

	// デバッグオブジェクトの描画
	InspectorDrawerCommon::DrawEntityDebugObject(*world, selected);

	ImGui::SetWindowFontScale(1.0f);

	ImGui::End();
}

void Engine::InspectorPanel::SyncNameBufferIfNeeded(ECSWorld& world, const Entity& entity) {

	// エンティティが存在しない場合は何もしない
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

void Engine::InspectorPanel::DrawEntityHeader(const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity) {

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

			context.host->ExecuteEditorCommand(std::make_unique<RenameEntityCommand>(entity, nameEditBuffer_));
		}
	}

	// アクティブ中は同期
	if (!editResult.anyItemActive) {
		SyncNameBufferIfNeeded(world, entity);
	}
	ImGui::Spacing();
	ImGui::Separator();
}

void Engine::InspectorPanel::DrawComponentToolbar(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	float spacing = ImGui::GetStyle().ItemSpacing.x;
	float width = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;

	if (!context.CanEditScene()) {
		ImGui::BeginDisabled();
	}

	// コンポーネントの追加、削除のボタンを表示する
	if (ImGui::Button("Add Component", ImVec2(width, 0.0f))) {
		ImGui::OpenPopup("##Inspector_AddComponentPopup");
	}
	ImGui::SameLine();
	if (ImGui::Button("Remove Component", ImVec2(width, 0.0f))) {
		ImGui::OpenPopup("##Inspector_RemoveComponentPopup");
	}

	if (!context.CanEditScene()) {
		ImGui::EndDisabled();
	}

	// 追加、削除のポップアップを表示する
	DrawAddComponentPopup(context, world, entity);
	DrawRemoveComponentPopup(context, world, entity);

	ImGui::Spacing();
	ImGui::Separator();
}

void Engine::InspectorPanel::DrawAddComponentPopup(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	if (!ImGui::BeginPopup("##Inspector_AddComponentPopup")) {
		return;
	}

	// 追加できるコンポーネントのメニューを表示する
	bool hasAny = false;
	for (const auto& entry : kOptionalComponentMenuEntries) {

		// すでに持っているコンポーネントは追加できない
		if (!IsScriptMenuEntry(entry) && world.HasComponent(entity, entry.typeName)) {
			continue;
		}

		hasAny = true;
		if (ImGui::MenuItem(entry.menuLabel)) {

			if (IsScriptMenuEntry(entry)) {

				context.host->ExecuteEditorCommand(std::make_unique<AddScriptEntryCommand>(entity));
			} else {

				context.host->ExecuteEditorCommand(std::make_unique<AddComponentCommand>(entity, entry.typeName));
			}
			ImGui::CloseCurrentPopup();
		}
	}
	// 追加できるコンポーネントがない
	if (!hasAny) {
		ImGui::TextDisabled("No components can be added.");
	}
	ImGui::EndPopup();
}

void Engine::InspectorPanel::DrawRemoveComponentPopup(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	if (!ImGui::BeginPopup("##Inspector_RemoveComponentPopup")) {
		return;
	}

	// 削除できるコンポーネントのメニューを表示する
	bool hasAny = false;
	for (const auto& entry : kOptionalComponentMenuEntries) {

		// 持っていないコンポーネントは削除できない
		if (!world.HasComponent(entity, entry.typeName)) {
			continue;
		}

		hasAny = true;
		if (ImGui::MenuItem(entry.menuLabel)) {

			context.host->ExecuteEditorCommand(std::make_unique<RemoveComponentCommand>(entity, entry.typeName));
			ImGui::CloseCurrentPopup();
		}
	}
	// 削除できるコンポーネントがない
	if (!hasAny) {
		ImGui::TextDisabled("No removable components.");
	}

	ImGui::EndPopup();
}

void Engine::InspectorPanel::DrawSelectedSubMeshHeader(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	// サブメッシュが選択されていることを前提に、サブメッシュの情報を表示する
	if (!context.editorState || !context.editorState->HasValidSubMeshSelection(&world)) {
		return;
	}
	if (!world.HasComponent<MeshRendererComponent>(entity)) {
		return;
	}

	const auto& meshRenderer = world.GetComponent<MeshRendererComponent>(entity);

	// 選択されているサブメッシュのインデックスを取得
	uint32_t subMeshIndex = 0;
	if (!context.editorState->TryResolveSelectedSubMeshIndex(&world, subMeshIndex)) {
		return;
	}
	if (meshRenderer.subMeshes.size() <= subMeshIndex) {
		return;
	}

	const auto& subMesh = meshRenderer.subMeshes[subMeshIndex];

	// エンティティ名とサブメッシュ名を決定
	std::string entityName = world.HasComponent<NameComponent>(entity) ?
		world.GetComponent<NameComponent>(entity).name : "Entity";
	std::string subMeshName = subMesh.name.empty() ?
		("SubMesh_" + std::to_string(subMesh.sourceSubMeshIndex)) : subMesh.name;

	ImGui::TextDisabled("Selected Target : Mesh SubMesh");
	ImGui::Text("Owner Entity : %s", entityName.c_str());
	ImGui::Text("SubMesh : [%u] %s", subMeshIndex, subMeshName.c_str());

	if (ImGui::Button("Back To Entity")) {

		context.editorState->SelectEntity(entity);
	}
	ImGui::Spacing();
	ImGui::Separator();
}
