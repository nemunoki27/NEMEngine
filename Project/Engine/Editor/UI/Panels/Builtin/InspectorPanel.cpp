#include "InspectorPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/EntityPropertyCommands.h>
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>
#include <Engine/Editor/Settings/ProjectTagSettings.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Tools/Registry/ToolRegistry.h>
#include <Engine/Editor/Commands/Components/AddComponentCommand.h>
#include <Engine/Editor/Commands/Components/RemoveComponentCommand.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Scripting/DragDrop/ScriptAssetDragDrop.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Rendering/Meshes/SkeletonBuilder.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Editor/Tools/Builtin/Camera/SceneViewCameraController.h>
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/BillboardComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Camera/CameraControllerComponent.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Asset/TextureAssetInspectorDrawer.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Prefab/Override/PrefabJsonDiff.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Editor/Utility/JointAttachmentUtility.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Editor/UI/Inspectors/Builtin/BuiltinComponentEditorRegistration.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Render/MeshRendererInspectorDrawer.h>

// c++
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <vector>
#include <span>

#include <Engine/Core/Rendering/Meshes/Import/AssimpMaterialTextureExtractor.h>
#include <Engine/Editor/Assets/Preview/ModelPreviewUtility.h>

//============================================================================
//	InspectorPanel classMethods
//============================================================================
namespace {

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
			if (text.empty() || Engine::TryParseAssetGUID32Hex(text)) {
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

	// MaterialPassKindの編集フィールドを描画する
	Engine::ValueEditResult DrawMaterialPassKindField(const char* label, Engine::MaterialPassKind& value) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		Engine::MaterialPassKind edited = value;
		result.valueChanged = Engine::EnumAdapter<Engine::MaterialPassKind>::Combo("##Value", &edited);
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
	void ApplyDefaultMeshMaterialTemplate(Engine::MaterialAsset& material) {

		material.domain = Engine::MaterialDomain::Surface;
		material.passes.clear();

		material.passes.push_back({
			.passKind = Engine::MaterialPassKind::ZPrepass,
			.pipeline = Engine::BuiltinAssets::Pipelines::DefaultMeshZPrepass,
			.preferredVariant = Engine::PipelineVariantKind::GraphicsMesh,
			});
		material.passes.push_back({
			.passKind = Engine::MaterialPassKind::Draw,
			.pipeline = Engine::BuiltinAssets::Pipelines::DefaultMesh,
			.preferredVariant = Engine::PipelineVariantKind::GraphicsMesh,
			});

		material.parameters.try_emplace("BaseColor", Engine::MaterialParameterValue{ .value = Engine::Color4::White() });
		material.parameters.try_emplace("Metallic", Engine::MaterialParameterValue{ .value = 0.0f });
		material.parameters.try_emplace("Roughness", Engine::MaterialParameterValue{ .value = 0.5f });
	}
}

Engine::InspectorPanel::InspectorPanel(const std::string& instanceID, bool primaryInstance) {

	ConfigureInstance("Inspector", instanceID, primaryInstance);
	modelPreviewCameraController_ = std::make_unique<SceneViewCameraController>();
	modelPreviewCameraController_->MakeDefaultState();
	modelPreviewCameraController_->SetSavePath(RuntimePaths::GetUserSettingsPath(
		ConfigPaths::kInspectorModelPreviewCamera).string());

	RegisterBuiltinComponentEditors(componentEditorRegistry_, meshRendererDrawer_);

	// アセット種別ごとのInspector表示を登録する
	assetInspectorRegistry_.Register(std::make_unique<TextureAssetInspectorDrawer>());
}

void Engine::InspectorPanel::DrawEditorTool([[maybe_unused]] const EditorToolContext& context) {

	// InspectorPanelはToolPanel上の独立ウィンドウを持たず、RenderTexture作成機能だけを利用する
}

nlohmann::json Engine::InspectorPanel::SaveLayoutState() const {

	return {
		{ "mode", lockedEntityUUID_ ? "LockedEntity" : "FollowSelection" },
		{ "lockedEntityUUID", lockedEntityUUID_ ? ToString(lockedEntityUUID_) : std::string{} },
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
	const bool entitySelection = context.editorState &&
		(context.editorState->selectionKind == EditorSelectionKind::Entity ||
			context.editorState->selectionKind == EditorSelectionKind::MeshSubMesh);
	if (!targetUUID && world && entitySelection && context.editorState->HasValidSelection(world)) {
		targetUUID = world->GetUUID(context.editorState->selectedEntity);
	}

	return {
		{ "mode", "LockedEntity" },
		{ "lockedEntityUUID", targetUUID ? ToString(targetUUID) : std::string{} },
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
	if (!context.editorState ||
		(context.editorState->selectionKind != EditorSelectionKind::Entity &&
			context.editorState->selectionKind != EditorSelectionKind::MeshSubMesh)) {
		return false;
	}
	return context.editorState->HasValidSelection(world);
}

void Engine::InspectorPanel::Draw(const EditorPanelContext& context) {

	// インスペクターパネルの表示状態を確認
	bool* open = ResolveOpenState(&context.layoutState->showInspector);
	if (!*open) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	Entity lockedEntity = Entity::Null();
	std::string displayName = "Inspector";
	if (lockedEntityUUID_) {

		lockedEntity = world ? world->FindByUUID(lockedEntityUUID_) : Entity::Null();
		displayName = world && world->IsAlive(lockedEntity) ?
			"Inspector: " + GetEntityDisplayName(*world, lockedEntity) : "Inspector: Missing Entity";
	}

	const std::string windowName = MakeWindowName(displayName);
	ApplyInitialDock();
	if (!ImGui::Begin(windowName.c_str(), open)) {
		DrawTitleBarContextMenu(context);
		ImGui::End();
		return;
	}
	DrawTitleBarContextMenu(context);

	// D&D中はImGui既定のホイールが効かないので、インスペクター上なら手動でスクロールする
	if (ImGui::GetDragDropPayload() != nullptr &&
		ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows)) {

		const float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0.0f) {
			ImGui::SetScrollY(ImGui::GetScrollY() - wheel * ImGui::GetFontSize() * 3.0f);
		}
	}

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
		DrawJointInspector(context);
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

		ImGui::TextDisabled("Entity is not selected.");
		ImGui::End();
		return;
	}

	// 選択されているエンティティを取得
	Entity selected = lockedEntityUUID_ ? lockedEntity : context.editorState->selectedEntity;

	ImGui::SetWindowFontScale(fontScale_);

	// サブメッシュが選択されている場合はサブメッシュのインスペクターを表示
	if (!lockedEntityUUID_ && context.editorState->HasValidSubMeshSelection(world)) {

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

	// プレファブインスタンスならオーバーライド一覧UIを描画する
	DrawPrefabOverrideUI(context, *world, selected);

	// 登録済みコンポーネント描画
	for (const auto& drawer : componentEditorRegistry_.GetDrawers()) {

		if (!drawer->CanDraw(*world, selected)) {
			continue;
		}
		drawer->Draw(context, *world, selected);
	}

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

	//============================================================================
	//	タグ、固定リストから選ぶ
	//============================================================================
	std::string currentTag = "Untagged";
	if (world.HasComponent<SceneObjectComponent>(entity)) {
		currentTag = world.GetComponent<SceneObjectComponent>(entity).tag;
	}

	const std::vector<std::string>& tags = ProjectTagSettings::GetTags();
	std::string editTag = currentTag;
	auto tagResult = MyGUI::StringCombo("Tag", editTag, std::span<const std::string>(tags.data(), tags.size()));
	if (tagResult.valueChanged && editTag != currentTag) {

		context.host->ExecuteEditorCommand(std::make_unique<SetEntityTagCommand>(entity, editTag));
	}

	// タグの追加削除はTag Managerツールで行う、Unityのタグ管理と同じ導線
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
	ImGui::Text("Path : %s", meta->assetPath.c_str());
	ImGui::Text("Type : %s", EnumAdapter<AssetType>::ToString(meta->type));
	ImGui::Text("ID   : %s", ToString(meta->guid).c_str());
	ImGui::Spacing();

	// Registryへ移行済みの種別はそちらへ委ねる、Textureはここに含まれる
	if (IAssetInspectorDrawer* drawer = assetInspectorRegistry_.Find(meta->type)) {

		drawer->Draw(context, *meta);
		return;
	}

	// Material/Meshは編集やプレビューでPanel内部状態を持つため当面ここに残す
	if (meta->type == AssetType::Material) {

		DrawMaterialAssetInspector(context, *meta);
		return;
	}
	if (meta->type == AssetType::Mesh) {

		DrawMeshAssetInspector(context, *meta);
		return;
	}

	ImGui::TextDisabled("No inspector for this asset type.");
}

void Engine::InspectorPanel::DrawMeshAssetInspector(const EditorPanelContext& context, const AssetMeta& meta) {

	if (!context.editorContext || !context.editorContext->assetDatabase || !context.editorState) {

		ImGui::TextDisabled("Mesh preview is not available.");
		return;
	}

	const uint64_t selectionRevision = context.editorState->assetSelectionRevision;
	if (modelPreviewAsset_ != meta.guid || modelPreviewSelectionRevision_ != selectionRevision) {

		modelPreviewSelectionRevision_ = selectionRevision;
		RebuildModelAssetPreviewWorld(context, meta);
	}

	ImGui::Text("Mesh Preview");
	ImGui::Separator();

	const float availableWidth = (std::max)(ImGui::GetContentRegionAvail().x, 64.0f);
	const float displayWidth = (std::min)(availableWidth, static_cast<float>(kModelPreviewSize_.x));
	const float displayHeight = displayWidth * static_cast<float>(kModelPreviewSize_.y) /
		static_cast<float>((std::max)(kModelPreviewSize_.x, 1));
	const ImVec2 displaySize(displayWidth, displayHeight);

	if (!context.graphicsCore || !context.renderPipeline || !modelPreviewWorld_ ||
		!modelPreviewWorld_->IsAlive(modelPreviewEntity_)) {

		ImGui::Dummy(displaySize);
		ImGui::TextDisabled("Mesh preview render target is not available.");
		return;
	}

	modelPreviewImagePos_ = ImGui::GetCursorScreenPos();
	Input::GetInstance()->SetViewRect(InputViewArea::InspectorModelPreview,
		Vector2(modelPreviewImagePos_.x, modelPreviewImagePos_.y),
		Vector2(displaySize.x, displaySize.y),
		EngineContext::GetWindowSetting().gameSize.GetFloat());

	ToolContext toolContext{};
	toolContext.world = context.editorContext->activeWorld;
	toolContext.assetDatabase = context.editorContext->assetDatabase;
	toolContext.sceneInstances = context.editorContext->sceneInstances;
	toolContext.activeSceneHeader = context.editorContext->activeSceneHeader;
	toolContext.activeSceneAsset = context.editorContext->activeSceneAsset;
	toolContext.activeSceneInstanceID = context.editorContext->activeSceneInstanceID;
	toolContext.activeScenePath = context.editorContext->activeScenePath;
	toolContext.isPlaying = context.IsPlaying();
	toolContext.canEditScene = context.CanEditScene();

	EditorToolContext editorToolContext{};
	editorToolContext.panelContext = &context;
	editorToolContext.toolContext = toolContext;

	BeginEditorToolFrame(editorToolContext);
	EditorToolRenderTexture* preview = CreateRenderTexture("InspectorModelAssetPreview",
		kModelPreviewSize_, kModelPreviewColor_, kModelPreviewColorTargetCount_);
	if (preview) {

		RenderModelAssetPreview(editorToolContext, *preview);
		ImGui::Image(preview->GetImTextureID(), displaySize);
	} else {

		ImGui::Dummy(displaySize);
		ImGui::TextDisabled("Mesh preview render target is not available.");
	}
	EndEditorToolFrame();
}

void Engine::InspectorPanel::RebuildModelAssetPreviewWorld(const EditorPanelContext& context, const AssetMeta& meta) {

	modelPreviewAsset_ = meta.guid;
	modelPreviewWorld_ = std::make_unique<ECSWorld>();
	modelPreviewEntity_ = Entity::Null();
	modelPreviewLightEntity_ = Entity::Null();
	modelPreviewBounds_ = ComputeModelAssetPreviewBounds(context, meta);

	AssetDatabase* database = context.editorContext ? context.editorContext->assetDatabase : nullptr;
	if (!database || !meta.guid) {

		ResetModelAssetPreviewCamera();
		return;
	}
	ModelPreviewUtility::ImportReferencedTextures(*database, meta.guid);

	Entity lightEntity = modelPreviewWorld_->CreateEntity(UUID::New());
	auto& lightTransform = modelPreviewWorld_->AddComponent<TransformComponent>(lightEntity);
	lightTransform.worldMatrix = Matrix4x4::Identity();
	lightTransform.isDirty = false;
	auto& light = modelPreviewWorld_->AddComponent<DirectionalLightComponent>(lightEntity);
	light.direction = Vector3(0.35f, -0.65f, 0.65f).Normalize();
	light.intensity = 1.5f;
	modelPreviewLightEntity_ = lightEntity;

	Entity entity = modelPreviewWorld_->CreateEntity(UUID::New());
	auto& transform = modelPreviewWorld_->AddComponent<TransformComponent>(entity);
	transform.worldMatrix = Matrix4x4::Identity();
	transform.isDirty = false;

	auto& renderer = modelPreviewWorld_->AddComponent<MeshRendererComponent>(entity);
	renderer.mesh = meta.guid;
	renderer.material = {};
	renderer.queue = RenderPhase::Opaque;
	renderer.visible = true;
	renderer.enableZPrepass = true;
	MeshSubMeshAuthoring::SyncComponent(database, renderer, false);

	modelPreviewEntity_ = entity;
	ResetModelAssetPreviewCamera();
}

void Engine::InspectorPanel::RenderModelAssetPreview(const EditorToolContext& toolContext,
	EditorToolRenderTexture& preview) {

	if (!toolContext.panelContext || !toolContext.panelContext->renderPipeline || !modelPreviewWorld_ ||
		!modelPreviewWorld_->IsAlive(modelPreviewEntity_)) {

		RenderToTexture(preview, [](EditorToolRenderContext&) {}, preview.clearColor);
		return;
	}

	RenderToTexture(preview, [&](EditorToolRenderContext& renderContext) {

		if (modelPreviewCameraController_) {

			modelPreviewCameraController_->Update(Dimension::Type3D, InputViewArea::InspectorModelPreview);
		}

		EntityPreviewRenderRequest request{};
		request.world = modelPreviewWorld_.get();
		request.systemContext = toolContext.toolContext.systemContext;
		request.assetDatabase = toolContext.toolContext.assetDatabase;
		request.sceneHeader = toolContext.toolContext.activeSceneHeader;
		request.sceneInstanceID = {};
		request.rootEntity = modelPreviewEntity_;
		request.surface = preview.GetRenderTarget();
		request.camera = modelPreviewCameraController_->GetCameraState();
		request.clearColor = preview.clearColor;
		request.drawGrid3D = true;

		toolContext.panelContext->renderPipeline->RenderEntityPreview(*renderContext.graphicsCore, request);
		}, preview.clearColor);
}

Engine::InspectorPanel::ModelAssetPreviewBounds Engine::InspectorPanel::ComputeModelAssetPreviewBounds(
	const EditorPanelContext& context, const AssetMeta& meta) const {

	ModelAssetPreviewBounds bounds{};
	const AssetDatabase* database = context.editorContext ? context.editorContext->assetDatabase : nullptr;
	if (database) {
		bounds.valid = ModelPreviewUtility::ComputeBounds(*database, meta.guid,
			bounds.min, bounds.max, bounds.center, bounds.radius);
	}
	return bounds;
}

void Engine::InspectorPanel::ResetModelAssetPreviewCamera() {

	if (!modelPreviewCameraController_) {
		return;
	}

	const Vector3 center = modelPreviewBounds_.valid ? modelPreviewBounds_.center : Vector3::AnyInit(0.0f);
	const float radius = (std::max)(modelPreviewBounds_.valid ? modelPreviewBounds_.radius : 1.0f, 0.1f);
	const float fovY = 35.0f;
	const float pitchDegrees = 8.0f;
	const float yawDegrees = 180.0f;
	const float aspectRatio = static_cast<float>(kModelPreviewSize_.x) /
		static_cast<float>((std::max)(kModelPreviewSize_.y, 1));
	const float distance = modelPreviewBounds_.valid ?
		ModelPreviewUtility::CalculateCameraDistance(modelPreviewBounds_.min, modelPreviewBounds_.max, center,
			pitchDegrees, yawDegrees, fovY, aspectRatio, 2.0f) :
		radius * 2.0f;
	const Matrix4x4 cameraRotation = Matrix4x4::MakeRotateMatrix(Vector3(pitchDegrees, yawDegrees, 0.0f));
	const Vector3 cameraForward(cameraRotation.m[2][0], cameraRotation.m[2][1], cameraRotation.m[2][2]);

	ManualRenderCameraState& camera = modelPreviewCameraController_->GetCameraState();
	camera = {};
	camera.enableOrthographic = false;
	camera.enablePerspective = true;
	camera.perspectiveFovY = fovY;
	camera.perspectiveNearClip = 0.01f;
	camera.perspectiveFarClip = (std::max)(4000.0f, distance + radius * 4.0f);
	camera.perspectiveCullingMask = -1;
	camera.transform3D.pos = center - cameraForward * distance;
	camera.transform3D.rotation = Vector3(pitchDegrees, yawDegrees, 0.0f);
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

		ApplyDefaultMeshMaterialTemplate(materialDraft_);
		saveRequested = true;
	}

	ImGui::Spacing();
	if (MyGUI::CollapsingHeader("Passes")) {

		int32_t removeIndex = -1;
		for (int32_t index = 0; index < static_cast<int32_t>(materialDraft_.passes.size()); ++index) {

			MaterialPassBinding& pass = materialDraft_.passes[index];
			ImGui::PushID(index);
			const std::string_view passLabel = EnumAdapter<MaterialPassKind>::ToStringView(pass.passKind);
			if (ImGui::TreeNodeEx("Pass", ImGuiTreeNodeFlags_DefaultOpen, "%.*s",
				static_cast<int>(passLabel.size()), passLabel.data())) {

				ValueEditResult passKindResult = DrawMaterialPassKindField("Pass Kind", pass.passKind);
				saveRequested |= passKindResult.editFinished;

				ValueEditResult pipelineResult = MyGUI::AssetReferenceField("Pipeline", pass.pipeline,
					context.editorContext->assetDatabase, { AssetType::RenderPipeline });
				saveRequested |= pipelineResult.editFinished;

				ValueEditResult shaderResult = MyGUI::AssetReferenceField("Shader Override", pass.shaderOverride,
					context.editorContext->assetDatabase, { AssetType::Shader });
				saveRequested |= shaderResult.editFinished;

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
				.passKind = MaterialPassKind::Draw,
				.pipeline = {},
				.preferredVariant = PipelineVariantKind::GraphicsMesh,
				});
			saveRequested = true;
		}
	}

	// シェーダーが要求するパラメータ名をリフレクションから集める、描画済みパイプラインのみ取得できる
	std::unordered_set<std::string> reflectedNames;
	std::vector<const ShaderReflectionInfo*> reflections;
	if (context.renderPipeline) {

		std::unordered_set<const ShaderReflectionInfo*> seenReflections;
		for (const MaterialPassBinding& pass : materialDraft_.passes) {

			if (!pass.pipeline) {
				continue;
			}
			MaterialAsset passMaterial{};
			passMaterial.passes.emplace_back(pass);
			const ShaderReflectionInfo* reflection = context.renderPipeline->FindMaterialDrawReflection(passMaterial);
			if (!reflection && !pass.shaderOverride) {
				reflection = context.renderPipeline->FindPipelineGraphicsReflection(pass.pipeline);
			}
			if (!reflection || seenReflections.count(reflection) != 0) {
				continue;
			}
			seenReflections.insert(reflection);
			reflections.push_back(reflection);
			if (const ShaderConstantBufferInfo* cb = FindConstantBuffer(*reflection, MaterialParameterCBuffer::kSurface)) {
				for (const ShaderConstantBufferVariable& var : cb->variables) {
					reflectedNames.insert(var.name);
				}
			}
			// space2のテクスチャSRVもマテリアルテクスチャとして自動列挙対象にする
			for (const ShaderResourceBinding& res : reflection->resources) {
				if (res.kind == ShaderBindingKind::SRV && res.space == 2 && res.rawType == D3D_SIT_TEXTURE) {
					reflectedNames.insert(res.name);
				}
			}
		}
	}

	// シェーダーのMaterialParameters cbufferを最初から編集可能な状態で自動列挙する
	ImGui::Spacing();
	if (MyGUI::CollapsingHeader("Shader Parameters", true)) {

		if (reflections.empty()) {

			ImGui::TextDisabled("シェーダー未構築か MaterialParameters cbuffer がありません");
			ImGui::TextDisabled("対象マテリアルが一度描画されると自動で列挙されます");
		} else {
			for (const ShaderReflectionInfo* reflection : reflections) {
				if (MaterialParameterEditor::DrawReflectedCBufferParameters(
					*reflection, MaterialParameterCBuffer::kSurface, materialDraft_.parameters)) {

					saveRequested = true;
				}
			}
		}
	}

	// space2のマテリアルテクスチャをリフレクションから自動列挙する、未指定なら描画時に白テクスチャになる
	ImGui::Spacing();
	if (MyGUI::CollapsingHeader("Shader Textures", true)) {

		std::unordered_set<std::string> drawnTextures;
		bool anyTexture = false;
		for (const ShaderReflectionInfo* reflection : reflections) {
			for (const ShaderResourceBinding& res : reflection->resources) {

				if (res.kind != ShaderBindingKind::SRV || res.space != 2 || res.rawType != D3D_SIT_TEXTURE) {
					continue;
				}
				if (drawnTextures.count(res.name) != 0) {
					continue;
				}
				drawnTextures.insert(res.name);
				anyTexture = true;

				AssetID textureID{};
				auto it = materialDraft_.parameters.find(res.name);
				if (it != materialDraft_.parameters.end()) {
					if (const AssetID* id = std::get_if<AssetID>(&it->second.value)) {
						textureID = *id;
					}
				}
				if (MyGUI::AssetReferenceField(res.name.c_str(), textureID,
					context.editorContext->assetDatabase, { AssetType::Texture }).editFinished) {

					materialDraft_.parameters[res.name].value = textureID;
					saveRequested = true;
				}
			}
		}
		if (!anyTexture) {
			ImGui::TextDisabled("space2のマテリアルテクスチャがありません");
		}
	}

	ImGui::Spacing();
	if (MyGUI::CollapsingHeader("Custom Parameters")) {

		std::string removeKey;
		for (auto& [key, parameter] : materialDraft_.parameters) {

			// シェーダーが要求するパラメータはShader Parametersで編集するため重複表示しない
			if (reflectedNames.count(key) != 0) {
				continue;
			}

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

	// 実行中のマテリアルキャッシュを破棄して編集を即反映する、エディタを止めずに調整できるようにする
	if (context.renderPipeline) {
		context.renderPipeline->ReloadMaterial(meta.guid);
	}
}

void Engine::InspectorPanel::DrawComponentToolbar(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	float spacing = ImGui::GetStyle().ItemSpacing.x;
	float width = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;

	if (!context.CanEditScene()) {
		ImGui::BeginDisabled();
	}

	// コンポーネントの追加、削除のボタンを表示する
	if (ImGui::Button("コンポーネント追加", ImVec2(width, 0.0f))) {
		ImGui::OpenPopup("##Inspector_AddComponentPopup");
	}
	ImGui::SameLine();
	if (ImGui::Button("コンポーネント削除", ImVec2(width, 0.0f))) {
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

void Engine::InspectorPanel::DrawComponentPopupEntries(const EditorPanelContext& context,
	TextSearchFilter& searchFilter, const char* searchInputID, const char* emptyText,
	const std::function<bool(const ComponentEditorDescriptor&)>& shouldShow,
	const std::function<void(const ComponentEditorDescriptor&)>& onSelect) {

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
		if (ImGui::MenuItem(entry.menuLabel.c_str())) {

			onSelect(entry);
			ImGui::CloseCurrentPopup();
		}
	}
	// 対象コンポーネントがない
	if (!hasAny) {
		ImGui::TextDisabled(emptyText);
	}
}

void Engine::InspectorPanel::DrawAddComponentPopup(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	if (!ImGui::BeginPopup("##Inspector_AddComponentPopup")) {
		return;
	}

	DrawComponentPopupEntries(context, addComponentSearchFilter_, "##AddComponentSearch",
		"No components can be added.",
		// すでに持っているコンポーネントは追加できない、複数追加を許可したものは除く
		[&](const ComponentEditorDescriptor& entry) { return componentEditorRegistry_.CanAdd(entry, world, entity); },
		[&](const ComponentEditorDescriptor& entry) {

			// Script追加など専用コマンドがあれば優先し、無ければ汎用追加コマンドを使う
			std::unique_ptr<IEditorCommand> command = componentEditorRegistry_.CreateAddCommand(entry, entity);
			if (!command) {

				command = std::make_unique<AddComponentCommand>(entity, entry.typeName);
			}
			context.host->ExecuteEditorCommand(std::move(command));
		});

	ImGui::EndPopup();
}

void Engine::InspectorPanel::DrawRemoveComponentPopup(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	if (!ImGui::BeginPopup("##Inspector_RemoveComponentPopup")) {
		return;
	}

	DrawComponentPopupEntries(context, removeComponentSearchFilter_, "##RemoveComponentSearch",
		"No removable components.",
		// 持っていないコンポーネントは削除できない
		[&](const ComponentEditorDescriptor& entry) { return world.HasComponent(entity, entry.typeName); },
		[&](const ComponentEditorDescriptor& entry) {

			context.host->ExecuteEditorCommand(std::make_unique<RemoveComponentCommand>(entity, entry.typeName));
		});

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

void Engine::InspectorPanel::DrawPrefabOverrideUI(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	// プレファブ編集中はUIを表示しない
	if (context.editorContext->isPrefabEditing) {
		return;
	}

	AssetDatabase* database = context.editorContext ? context.editorContext->assetDatabase : nullptr;
	if (!database || !world.HasComponent<PrefabLinkComponent>(entity)) {
		return;
	}
	const auto& link = world.GetComponent<PrefabLinkComponent>(entity);

	// インスタンス全体の差分を抽出する、ベースはファイル更新時刻でキャッシュして毎フレームの再読込を避ける
	const auto& base = PrefabOverrideUtility::LoadPrefabBaseEntitiesCached(*database, link.prefabAsset);
	PrefabInstanceData data = PrefabOverrideUtility::CaptureInstance(world, link.prefabInstanceID, base);
	data.prefabAsset = link.prefabAsset;
	const UUID sceneInstanceID = world.HasComponent<SceneObjectComponent>(entity) ?
		world.GetComponent<SceneObjectComponent>(entity).sceneInstanceID : UUID{};

	// SceneローカルIDから同じSceneのEntityを引く
	auto findSceneEntity = [&](UUID localFileID) -> Entity {

		Entity found = Entity::Null();
		world.ForEach<SceneObjectComponent>([&](const Entity& candidate, SceneObjectComponent& sceneObject) {

			if (!found.IsValid() && sceneObject.sceneInstanceID == sceneInstanceID &&
				sceneObject.localFileID == localFileID) {
				found = candidate;
			}
			});
		return found;
		};

	// 追加Entityは親も追加Entityなら子なのでルートだけを一覧に出す
	std::unordered_set<UUID> addedSceneLocalFileIDs;
	for (const auto& added : data.addedEntities) {
		addedSceneLocalFileIDs.insert(added.sceneLocalFileID);
	}
	std::vector<Entity> addedEntityRoots;
	for (const auto& added : data.addedEntities) {

		if (addedSceneLocalFileIDs.contains(added.parentSceneLocalFileID)) {
			continue;
		}
		const Entity addedRoot = findSceneEntity(added.sceneLocalFileID);
		if (world.IsAlive(addedRoot)) {
			addedEntityRoots.emplace_back(addedRoot);
		}
	}

	const int overrideCount = static_cast<int>(data.modifications.size() + data.addedComponents.size() +
		data.removedComponents.size() + addedEntityRoots.size() + data.removedEntities.size());

	// オーバーライド一覧を開くボタン、件数も出す
	const std::string buttonLabel = overrideCount > 0 ?
		("Prefab 上書きパラメータ (" + std::to_string(overrideCount) + ")###PrefabOverrideButton") :
		std::string("Prefab : 差分なし###PrefabOverrideButton");
	if (ImGui::Button(buttonLabel.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		overrideChoices_.clear();
		ImGui::OpenPopup("PrefabOverridesPopup");
	}
	ImGui::Spacing();

	if (!ImGui::BeginPopup("PrefabOverridesPopup")) {
		return;
	}

	const bool applyClicked = ImGui::Button("設定を適用");
	ImGui::Separator();

	// 各差分の選択ボタンを描画する、アクティブな選択を青で強調しデフォルトはそのまま
	auto drawChoice = [&](const std::string& key, bool allowApply) {

		int& choice = overrideChoices_[key];
		auto button = [&](const char* label, int value, bool enabled) {

			const bool active = (choice == value);
			if (active) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.05f, 0.95f, 1.0f)); }
			if (!enabled) { ImGui::BeginDisabled(); }
			if (ImGui::SmallButton((std::string(label) + "##" + key).c_str())) { choice = value; }
			if (!enabled) { ImGui::EndDisabled(); }
			if (active) { ImGui::PopStyleColor(); }
			};
		button("プレファブへ反映", 1, allowApply);
		ImGui::SameLine();
		button("元に戻す", 2, true);
		ImGui::SameLine();
		button("このインスタンスのみ維持", 0, true);
		};

	if (overrideCount == 0) {
		ImGui::TextDisabled("差分はありません");
	}

	// プロパティ差分
	for (const auto& mod : data.modifications) {

		const std::string key = "M|" + ToString(mod.target) + "|" + mod.path;
		const nlohmann::json* baseValue = nullptr;
		auto baseIt = base.find(mod.target);
		if (baseIt != base.end()) {
			baseValue = PrefabJsonDiff::GetAtPath(baseIt->second.components, mod.path);
		}
		ImGui::TextUnformatted(mod.path.c_str());
		const std::string valueText = (baseValue ? baseValue->dump() : std::string("(none)")) + "  ->  " + mod.value.dump();
		ImGui::TextDisabled("%s", valueText.c_str());
		drawChoice(key, true);
		ImGui::Separator();
	}
	// 追加コンポーネント
	for (const auto& added : data.addedComponents) {

		const std::string key = "AC|" + ToString(added.target) + "|" + added.type;
		ImGui::Text("+ %s  追加コンポーネント", added.type.c_str());
		drawChoice(key, true);
		ImGui::Separator();
	}
	// 削除コンポーネント
	for (const auto& removed : data.removedComponents) {

		const std::string key = "RC|" + ToString(removed.target) + "|" + removed.type;
		ImGui::Text("- %s  削除コンポーネント", removed.type.c_str());
		drawChoice(key, true);
		ImGui::Separator();
	}
	// 追加Entity
	for (const Entity& addedRoot : addedEntityRoots) {

		const UUID localFileID = world.GetComponent<SceneObjectComponent>(addedRoot).localFileID;
		const std::string key = "AE|" + ToString(localFileID);
		const std::string name = world.HasComponent<NameComponent>(addedRoot) ?
			world.GetComponent<NameComponent>(addedRoot).name : "Entity";
		const bool canApply = PrefabOverrideUtility::CanPromoteAddedEntitySubtree(
			world, addedRoot, link.prefabInstanceID);
		ImGui::Text("+ %s  追加Entity", name.c_str());
		drawChoice(key, canApply);
		if (!canApply) {
			ImGui::TextDisabled("Nested Prefabを含むサブツリーは反映できません");
		}
		ImGui::Separator();
	}
	// 旧Sceneの削除差分は復元用に読み込みを維持する
	for (size_t i = 0; i < data.removedEntities.size(); ++i) {
		ImGui::TextDisabled("- 取り除かれた子エンティティ");
	}

	// 適用ボタンで各差分の選択を反映する
	if (applyClicked) {

		// インスタンス内の対象エンティティを引く
		auto findInstanceEntity = [&](UUID target) -> Entity {

			for (const Entity& candidate : PrefabOverrideUtility::CollectInstanceEntities(world, link.prefabInstanceID)) {
				if (world.GetComponent<PrefabLinkComponent>(candidate).prefabLocalFileID == target) {
					return candidate;
				}
			}
			return Entity::Null();
			};

		// プレファブファイルを読み、Apply対象を書き込む
		const auto prefabPath = database->ResolveFullPath(link.prefabAsset);
		nlohmann::json prefabFileJson = JsonAdapter::Load(prefabPath.string(), true);
		const auto oldBase = base;
		bool prefabChanged = false;
		bool instanceHierarchyChanged = false;

		for (const auto& mod : data.modifications) {

			const std::string key = "M|" + ToString(mod.target) + "|" + mod.path;
			const int choice = overrideChoices_.count(key) ? overrideChoices_[key] : 0;
			if (choice == 1) {

				prefabChanged |= PrefabOverrideUtility::SetPrefabEntityLeaf(prefabFileJson, mod.target, mod.path, mod.value);
			} else if (choice == 2) {

				// インスタンスの値をベースへ戻す、伝播時に差分が消えて元に戻る
				const Entity target = findInstanceEntity(mod.target);
				auto baseIt = base.find(mod.target);
				if (world.IsAlive(target) && baseIt != base.end()) {

					const nlohmann::json* baseValue = PrefabJsonDiff::GetAtPath(baseIt->second.components, mod.path);
					if (baseValue) {

						const size_t slash = mod.path.find('/');
						const std::string type = (slash == std::string::npos) ? mod.path : mod.path.substr(0, slash);
						const std::string leaf = (slash == std::string::npos) ? std::string{} : mod.path.substr(slash + 1);
						nlohmann::json current;
						world.SerializeComponentToJson(target, type, current);
						PrefabJsonDiff::SetAtPath(current, leaf, *baseValue);
						world.AddComponentFromJson(target, type, current);
					}
				}
			}
		}
		for (const auto& added : data.addedComponents) {

			const std::string key = "AC|" + ToString(added.target) + "|" + added.type;
			const int choice = overrideChoices_.count(key) ? overrideChoices_[key] : 0;
			if (choice == 1) {

				prefabChanged |= PrefabOverrideUtility::SetPrefabEntityComponent(prefabFileJson, added.target, added.type, added.value);
			} else if (choice == 2) {

				const Entity target = findInstanceEntity(added.target);
				if (world.IsAlive(target)) { world.RemoveComponentByName(target, added.type); }
			}
		}
		for (const auto& removed : data.removedComponents) {

			const std::string key = "RC|" + ToString(removed.target) + "|" + removed.type;
			const int choice = overrideChoices_.count(key) ? overrideChoices_[key] : 0;
			if (choice == 1) {

				prefabChanged |= PrefabOverrideUtility::RemovePrefabEntityComponent(prefabFileJson, removed.target, removed.type);
			} else if (choice == 2) {

				const Entity target = findInstanceEntity(removed.target);
				auto baseIt = base.find(removed.target);
				if (world.IsAlive(target) && baseIt != base.end() && baseIt->second.components.contains(removed.type)) {
					world.AddComponentFromJson(target, removed.type, baseIt->second.components[removed.type]);
				}
			}
		}

		// 追加Entityはサブツリー単位でPrefabへ反映または破棄する
		std::vector<Entity> addedRootsToApply;
		std::vector<Entity> addedRootsToRevert;
		for (const Entity& addedRoot : addedEntityRoots) {

			if (!world.IsAlive(addedRoot) || !world.HasComponent<SceneObjectComponent>(addedRoot)) {
				continue;
			}
			const UUID localFileID = world.GetComponent<SceneObjectComponent>(addedRoot).localFileID;
			const std::string key = "AE|" + ToString(localFileID);
			const int choice = overrideChoices_.count(key) ? overrideChoices_[key] : 0;
			if (choice == 1 && PrefabOverrideUtility::CanPromoteAddedEntitySubtree(
				world, addedRoot, link.prefabInstanceID)) {
				addedRootsToApply.emplace_back(addedRoot);
			} else if (choice == 2) {
				addedRootsToRevert.emplace_back(addedRoot);
			}
		}
		if (!addedRootsToApply.empty()) {

			prefabChanged |= PrefabOverrideUtility::PromoteAddedEntitySubtrees(
				prefabFileJson, world, link.prefabAsset, link.prefabInstanceID, addedRootsToApply);
		}
		for (const Entity& addedRoot : addedRootsToRevert) {

			if (world.IsAlive(addedRoot)) {
				EditorEntitySnapshotUtility::DestroySubtree(world, addedRoot);
				instanceHierarchyChanged = true;
			}
		}

		if (prefabChanged) {
			PrefabReferenceRemapper::NormalizePrefabFileHierarchy(prefabFileJson);
			PrefabReferenceRemapper::NormalizePrefabFileJointAttachments(prefabFileJson);
			JsonAdapter::Save(prefabPath.string(), prefabFileJson);
		}
		if (instanceHierarchyChanged && !prefabChanged) {

			std::vector<Entity> hierarchyScope;
			world.ForEachAliveEntity([&](Entity candidate) { hierarchyScope.emplace_back(candidate); });
			HierarchySystem hierarchySystem{};
			hierarchySystem.RebuildRuntimeLinks(world, hierarchyScope);
		}
		if (prefabChanged) {

			// 変更を全インスタンスへ伝播し、関係ないOverrideは保持する
			HierarchySystem hierarchySystem{};
			PrefabOverrideUtility::PropagateToInstances(world, *database, hierarchySystem, link.prefabAsset, oldBase);
		}

		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Engine::InspectorPanel::DrawJointInspector(const EditorPanelContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || !context.editorState) {
		return;
	}
	const Entity skinned = context.editorState->selectedJointSkinnedEntity;
	const int32_t jointIndex = context.editorState->selectedJointIndex;
	if (!world->IsAlive(skinned) || !world->HasComponent<SkinnedAnimationComponent>(skinned)) {

		ImGui::TextDisabled("ジョイントが無効です");
		return;
	}
	const Skeleton& skeleton = world->GetComponent<SkinnedAnimationComponent>(skinned).runtimeSkeleton;
	if (jointIndex < 0 || jointIndex >= static_cast<int32_t>(skeleton.joints.size())) {

		ImGui::TextDisabled("ジョイントが無効です");
		return;
	}
	const Joint& joint = skeleton.joints[jointIndex];

	// ジョイントの基本情報を出す、リネーム等はしない
	ImGui::Text("Joint : %s", joint.name.empty() ? "(no name)" : joint.name.c_str());
	ImGui::Text("Index : %d", jointIndex);
	if (joint.parent && *joint.parent >= 0 && *joint.parent < static_cast<int32_t>(skeleton.joints.size())) {
		ImGui::Text("親 : %s", skeleton.joints[*joint.parent].name.c_str());
	} else {
		ImGui::TextDisabled("親 : (root)");
	}

	// このジョイントへ親子付けされた子エンティティがあるか調べる
	UUID skinnedLocalFileID{};
	if (world->HasComponent<SceneObjectComponent>(skinned)) {
		skinnedLocalFileID = world->GetComponent<SceneObjectComponent>(skinned).localFileID;
	}
	bool hasAttachedEntity = false;
	if (skinnedLocalFileID) {
		world->ForEachAliveEntity([&](Entity other) {

			if (hasAttachedEntity || !world->HasComponent<JointAttachmentComponent>(other)) {
				return;
			}
			const auto& attachment = world->GetComponent<JointAttachmentComponent>(other);
			const int32_t attachedJointIndex =
				FindSkeletonJointIndex(skeleton, attachment.jointName);
			if (attachment.skinnedEntityLocalFileID == skinnedLocalFileID &&
				attachedJointIndex == jointIndex) {
				hasAttachedEntity = true;
			}
			});
	}

	// 子エンティティがある場合は、ジョイントのワールド行列をTransformの行列表示と同じ形で出す
	if (hasAttachedEntity) {

		ImGui::Spacing();
		ImGui::Separator();
		Matrix4x4 jointWorld{};
		if (JointAttachmentUtility::GetJointWorldMatrix(*world, skinned, joint.name, jointWorld)) {
			MyGUI::TextMatrix4x4("ワールド行列", jointWorld);
		}
	}
}
