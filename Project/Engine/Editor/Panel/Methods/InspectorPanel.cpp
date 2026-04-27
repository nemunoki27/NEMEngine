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
#include <Engine/Core/Asset/AssetDatabase.h>
#include <Engine/Core/Graphics/Asset/MaterialAsset.h>
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
#include <Engine/Utility/Enum/EnumAdapter.h>
#include <Engine/Utility/Json/JsonAdapter.h>
#include <Engine/Logger/Logger.h>

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
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <string_view>
#include <type_traits>

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

	// 16桁hexのAssetID文字列かどうかを判定する
	bool LooksLikeAssetID(const std::string& text) {

		if (text.size() != 16) {
			return false;
		}
		return std::all_of(text.begin(), text.end(), [](unsigned char c) {
			return std::isxdigit(c) != 0;
			});
	}

	// Material JSON内のパス参照をAssetIDに解決して、Inspectorで編集できる形にする
	void ResolveMaterialPipelineReferences(Engine::AssetDatabase& database, nlohmann::json& data) {

		if (!data.contains("passes") || !data["passes"].is_array()) {
			return;
		}
		for (auto& passJson : data["passes"]) {

			if (!passJson.is_object() || !passJson["pipeline"].is_string()) {
				continue;
			}

			const std::string text = passJson["pipeline"].get<std::string>();
			if (text.empty() || LooksLikeAssetID(text)) {
				continue;
			}

			const std::filesystem::path fullPath = database.ResolveAssetPath(text);
			if (!std::filesystem::exists(fullPath)) {
				continue;
			}

			const Engine::AssetID pipeline = database.ImportOrGet(text, Engine::AssetType::RenderPipeline);
			if (pipeline) {
				passJson["pipeline"] = Engine::ToString(pipeline);
			}
		}
	}

	// MaterialDomainの編集フィールドを描画する
	Engine::ValueEditResult DrawMaterialDomainField(const char* label, Engine::MaterialDomain& value) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		Engine::MaterialDomain edited = value;
		result.valueChanged = Engine::EnumAdapter<Engine::MaterialDomain>::Combo("##Value", &edited);
		if (result.valueChanged) {
			value = edited;
		}
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = result.valueChanged || ImGui::IsItemDeactivatedAfterEdit();
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	// PipelineVariantKindの編集フィールドを描画する
	Engine::ValueEditResult DrawPipelineVariantField(const char* label, Engine::PipelineVariantKind& value) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		Engine::PipelineVariantKind edited = value;
		result.valueChanged = Engine::EnumAdapter<Engine::PipelineVariantKind>::Combo("##Value", &edited);
		if (result.valueChanged) {
			value = edited;
		}
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = result.valueChanged || ImGui::IsItemDeactivatedAfterEdit();
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	// Materialパラメーターの型名を表示用に取得する
	const char* GetMaterialParameterTypeName(const Engine::MaterialParameterValue& parameter) {

		return std::visit([](const auto& value) -> const char* {
			using ValueType = std::decay_t<decltype(value)>;

			if constexpr (std::is_same_v<ValueType, float>) {
				return "Float";
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector2>) {
				return "Vector2";
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector3>) {
				return "Vector3";
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector4>) {
				return "Vector4";
			} else if constexpr (std::is_same_v<ValueType, Engine::Color4>) {
				return "Color4";
			} else if constexpr (std::is_same_v<ValueType, Engine::AssetID>) {
				return "Asset";
			} else if constexpr (std::is_same_v<ValueType, int32_t>) {
				return "Int";
			} else if constexpr (std::is_same_v<ValueType, uint32_t>) {
				return "UInt";
			} else {
				return "Unknown";
			}
			}, parameter.value);
	}

	// Materialパラメーターの値を型に応じたUIで描画する
	Engine::ValueEditResult DrawMaterialParameterValue(const char* label,
		Engine::MaterialParameterValue& parameter, const Engine::AssetDatabase* assetDatabase) {

		return std::visit([&](auto& value) -> Engine::ValueEditResult {
			using ValueType = std::decay_t<decltype(value)>;

			if constexpr (std::is_same_v<ValueType, float>) {
				return Engine::MyGUI::DragFloat(label, value);
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector2>) {
				return Engine::MyGUI::DragVector2(label, value);
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector3>) {
				return Engine::MyGUI::DragVector3(label, value);
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector4>) {
				return Engine::MyGUI::DragVector4(label, value);
			} else if constexpr (std::is_same_v<ValueType, Engine::Color4>) {
				return Engine::MyGUI::ColorEdit(label, value);
			} else if constexpr (std::is_same_v<ValueType, Engine::AssetID>) {
				return Engine::MyGUI::AssetReferenceField(label, value, assetDatabase, { Engine::AssetType::Texture });
			} else if constexpr (std::is_same_v<ValueType, int32_t>) {
				return Engine::MyGUI::DragInt(label, value);
			} else if constexpr (std::is_same_v<ValueType, uint32_t>) {
				int32_t signedValue = static_cast<int32_t>(value);
				Engine::ValueEditResult result = Engine::MyGUI::DragInt(label, signedValue,
					{ .dragSpeed = 1.0f, .minValue = 0, .maxValue = (std::numeric_limits<int32_t>::max)() });
				if (result.valueChanged) {
					value = static_cast<uint32_t>(signedValue);
				}
				return result;
			} else {
				return Engine::ValueEditResult{};
			}
			}, parameter.value);
	}

	// よく使うMesh用MaterialのPass構成を設定する
	void ApplyDefaultMeshMaterialTemplate(Engine::MaterialAsset& material, const Engine::AssetDatabase* assetDatabase) {

		material.domain = Engine::MaterialDomain::Surface;
		material.passes.clear();

		auto resolvePipeline = [&](const char* path) {
			if (!assetDatabase) {
				return Engine::AssetID{};
			}
			const Engine::AssetMeta* meta = assetDatabase->FindByPath(path);
			return meta ? meta->guid : Engine::AssetID{};
			};

		material.passes.push_back({
			.passName = "ZPrepass",
			.pipeline = resolvePipeline("Engine/Assets/Pipelines/Builtin/Mesh/defaultMeshZPrepass.pipeline.json"),
			.preferredVariant = Engine::PipelineVariantKind::GraphicsMesh,
			});
		material.passes.push_back({
			.passName = "Draw",
			.pipeline = resolvePipeline("Engine/Assets/Pipelines/Builtin/Mesh/defaultMesh.pipeline.json"),
			.preferredVariant = Engine::PipelineVariantKind::GraphicsMesh,
			});

		material.parameters.try_emplace("BaseColor", Engine::MaterialParameterValue{ .value = Engine::Color4::White() });
		material.parameters.try_emplace("Metallic", Engine::MaterialParameterValue{ .value = 0.0f });
		material.parameters.try_emplace("Roughness", Engine::MaterialParameterValue{ .value = 0.5f });
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
	if (context.editorState->selectionKind == EditorSelectionKind::Asset) {

		ImGui::SetWindowFontScale(fontScale_);
		DrawSelectedAssetInspector(context);
		ImGui::SetWindowFontScale(1.0f);
		ImGui::End();
		return;
	}

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
	ImGui::Text("Path : %s", meta->assetPath.c_str());
	ImGui::Text("Type : %s", EnumAdapter<AssetType>::ToString(meta->type));
	ImGui::Text("ID   : %s", ToString(meta->guid).c_str());
	ImGui::Spacing();

	if (meta->type == AssetType::Material) {

		DrawMaterialAssetInspector(context, *meta);
		return;
	}

	ImGui::TextDisabled("No inspector for this asset type.");
}

void Engine::InspectorPanel::DrawMaterialAssetInspector(const EditorPanelContext& context, const AssetMeta& meta) {

	if (!LoadMaterialDraft(context, meta)) {

		ImGui::TextDisabled("Failed to load material.");
		return;
	}

	bool saveRequested = false;

	ImGui::Text("Material");
	ImGui::Separator();

	ValueEditResult nameResult = MyGUI::InputText("Name", materialDraft_.name);
	saveRequested |= nameResult.editFinished;

	ValueEditResult domainResult = DrawMaterialDomainField("Domain", materialDraft_.domain);
	saveRequested |= domainResult.editFinished;

	if (ImGui::Button("Use Mesh Template", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

		ApplyDefaultMeshMaterialTemplate(materialDraft_, context.editorContext->assetDatabase);
		saveRequested = true;
	}

	ImGui::Spacing();
	if (MyGUI::CollapsingHeader("Passes")) {

		int32_t removeIndex = -1;
		for (int32_t index = 0; index < static_cast<int32_t>(materialDraft_.passes.size()); ++index) {

			MaterialPassBinding& pass = materialDraft_.passes[index];
			ImGui::PushID(index);
			if (ImGui::TreeNodeEx("Pass", ImGuiTreeNodeFlags_DefaultOpen, "%s", pass.passName.empty() ? "Unnamed Pass" : pass.passName.c_str())) {

				ValueEditResult passNameResult = MyGUI::InputText("Pass Name", pass.passName);
				saveRequested |= passNameResult.editFinished;

				ValueEditResult pipelineResult = MyGUI::AssetReferenceField("Pipeline", pass.pipeline,
					context.editorContext->assetDatabase, { AssetType::RenderPipeline });
				saveRequested |= pipelineResult.editFinished;

				ValueEditResult variantResult = DrawPipelineVariantField("Variant", pass.preferredVariant);
				saveRequested |= variantResult.editFinished;

				if (ImGui::Button("Remove Pass", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

					removeIndex = index;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (0 <= removeIndex && removeIndex < static_cast<int32_t>(materialDraft_.passes.size())) {

			materialDraft_.passes.erase(materialDraft_.passes.begin() + removeIndex);
			saveRequested = true;
		}
		if (ImGui::Button("Add Pass", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			materialDraft_.passes.push_back({
				.passName = "Draw",
				.pipeline = {},
				.preferredVariant = PipelineVariantKind::GraphicsMesh,
				});
			saveRequested = true;
		}
	}

	ImGui::Spacing();
	if (MyGUI::CollapsingHeader("Parameters")) {

		std::string removeKey;
		for (auto& [key, parameter] : materialDraft_.parameters) {

			ImGui::PushID(key.c_str());
			ImGui::TextDisabled("%s", GetMaterialParameterTypeName(parameter));
			ValueEditResult parameterResult = DrawMaterialParameterValue(key.c_str(), parameter,
				context.editorContext->assetDatabase);
			saveRequested |= parameterResult.editFinished;
			if (ImGui::Button("Remove", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

				removeKey = key;
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (!removeKey.empty()) {

			materialDraft_.parameters.erase(removeKey);
			saveRequested = true;
		}

		if (ImGui::Button("Add BaseColor", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			materialDraft_.parameters["BaseColor"] = MaterialParameterValue{ .value = Color4::White() };
			saveRequested = true;
		}
		if (ImGui::Button("Add MainTexture", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			materialDraft_.parameters["MainTexture"] = MaterialParameterValue{ .value = AssetID{} };
			saveRequested = true;
		}
		if (ImGui::Button("Add Float", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {

			std::string name = "Float";
			uint32_t suffix = 1;
			while (materialDraft_.parameters.contains(name)) {
				name = "Float" + std::to_string(suffix++);
			}
			materialDraft_.parameters[name] = MaterialParameterValue{ .value = 0.0f };
			saveRequested = true;
		}
	}

	if (saveRequested) {

		SaveMaterialDraft(context, meta);
	}
}

bool Engine::InspectorPanel::LoadMaterialDraft(const EditorPanelContext& context, const AssetMeta& meta) {

	if (materialDraftValid_ && editingMaterialAsset_ == meta.guid) {
		return true;
	}

	materialDraftValid_ = false;
	editingMaterialAsset_ = meta.guid;
	materialDraft_ = MaterialAsset{};

	if (!context.editorContext || !context.editorContext->assetDatabase) {
		return false;
	}

	const std::filesystem::path path = context.editorContext->assetDatabase->ResolveFullPath(meta.guid);
	if (path.empty()) {
		return false;
	}

	nlohmann::json data = JsonAdapter::Load(path.string(), false);
	ResolveMaterialPipelineReferences(*context.editorContext->assetDatabase, data);
	if (!FromJson(data, materialDraft_)) {
		return false;
	}
	if (!materialDraft_.guid) {
		materialDraft_.guid = meta.guid;
	}
	if (materialDraft_.name.empty()) {
		materialDraft_.name = path.stem().stem().string();
	}

	materialDraftValid_ = true;
	return true;
}

void Engine::InspectorPanel::SaveMaterialDraft(const EditorPanelContext& context, const AssetMeta& meta) {

	if (!materialDraftValid_ || !context.editorContext || !context.editorContext->assetDatabase) {
		return;
	}

	materialDraft_.guid = meta.guid;

	const std::filesystem::path path = context.editorContext->assetDatabase->ResolveFullPath(meta.guid);
	if (path.empty()) {
		return;
	}

	JsonAdapter::Save(path.string(), ToJson(materialDraft_));
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
