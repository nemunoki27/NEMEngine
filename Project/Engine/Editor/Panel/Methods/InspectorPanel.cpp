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
#include <Engine/Core/Graphics/Mesh/MeshSubMeshAuthoring.h>
#include <Engine/Core/Graphics/Render/RenderPipelineRunner.h>
#include <Engine/Core/Graphics/Render/View/SceneViewCameraController.h>
#include <Engine/Core/Graphics/Texture/TextureAssetResolver.h>
#include <Engine/Core/Context/EngineContext.h>
#include <Engine/Core/Runtime/RuntimePaths.h>
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Core/ECS/Component/Builtin/ScriptComponent.h>
#include <Engine/Core/ECS/Component/Builtin/AudioSourceComponent.h>
#include <Engine/Core/ECS/Component/Builtin/CollisionComponent.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Light/DirectionalLightComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/SpriteRendererComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/TextRendererComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/ECS/Component/Builtin/CameraComponent.h>
#include <Engine/Core/ECS/Component/Builtin/CameraControllerComponent.h>
#include <Engine/Editor/Command/Methods/SetEntityActiveCommand.h>
#include <Engine/Editor/Inspector/Methods/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Utility/ImGui/MyGUI.h>
#include <Engine/Core/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Utility/Json/JsonAdapter.h>
#include <Engine/Core/Logger/Logger.h>
#include <Engine/Core/Input/Input.h>

// assimp
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

// インスペクター描画
#include <Engine/Editor/Inspector/Methods/TransformInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/UVTransformInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/ScriptInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/CameraInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/CameraControllerInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/Render/SpriteRendererInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/Render/MeshRendererInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/Render/TextRendererInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/Light/DirectionalLightInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/Light/PointLightInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/Light/SpotLightInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/Animation/SkinnedAnimationInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/Audio/AudioSourceInspectorDrawer.h>
#include <Engine/Editor/Inspector/Methods/CollisionInspectorDrawer.h>

// c++
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <type_traits>
#include <vector>

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
	constexpr std::array<InspectorComponentMenuEntry, 14> kOptionalComponentMenuEntries = { {

		{ "PerspectiveCamera",  "PerspectiveCamera" },
		{ "OrthographicCamera", "OrthographicCamera" },
		{ "Camera Controller",  "CameraController" },
		{ "Script",             "Script" },
		{ "Audio Source",       "AudioSource" },
		{ "Collision",          "Collision" },
		{ "Mesh Renderer",      "MeshRenderer" },
		{ "Skinned Animation",  "SkinnedAnimation" },
		{ "Sprite Renderer",    "SpriteRenderer" },
		{ "Text Renderer",      "TextRenderer" },
		{ "UVTransform",      "UVTransform" },
		{ "DirectionalLight", "DirectionalLight" },
		{ "PointLight",       "PointLight" },
		{ "SpotLight",        "SpotLight" },
	} };
	constexpr uint32_t kModelPreviewAssimpFlags =
		aiProcess_FlipWindingOrder |
		aiProcess_FlipUVs |
		aiProcess_Triangulate |
		aiProcess_GenSmoothNormals |
		aiProcess_CalcTangentSpace |
		aiProcess_JoinIdenticalVertices |
		aiProcess_ImproveCacheLocality |
		aiProcess_SortByPType;

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
	Engine::Vector3 ToEnginePreviewPosition(const aiVector3D& pos) {

		return Engine::Vector3(-pos.x, pos.y, pos.z);
	}
	std::string GetMaterialTextureReference(aiMaterial* material, std::initializer_list<aiTextureType> textureTypes) {

		if (!material) {
			return {};
		}
		for (aiTextureType type : textureTypes) {

			if (material->GetTextureCount(type) == 0) {
				continue;
			}

			aiString textureName;
			if (material->GetTexture(type, 0, &textureName) == AI_SUCCESS && 0 < textureName.length) {
				return textureName.C_Str();
			}
		}
		return {};
	}
	void CollectAssimpNodePositions(const aiScene* scene, const aiNode* node, const aiMatrix4x4& parentTransform,
		std::vector<Engine::Vector3>& outPositions) {

		(void)parentTransform;
		if (!scene || !node) {
			return;
		}

		for (uint32_t meshRefIndex = 0; meshRefIndex < node->mNumMeshes; ++meshRefIndex) {

			const uint32_t meshIndex = node->mMeshes[meshRefIndex];
			if (scene->mNumMeshes <= meshIndex) {
				continue;
			}

			const aiMesh* mesh = scene->mMeshes[meshIndex];
			if (!mesh || mesh->mNumVertices == 0) {
				continue;
			}

			outPositions.reserve(outPositions.size() + mesh->mNumVertices);
			for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {

				outPositions.emplace_back(ToEnginePreviewPosition(mesh->mVertices[vertexIndex]));
			}
		}

		for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {

			CollectAssimpNodePositions(scene, node->mChildren[childIndex], aiMatrix4x4(), outPositions);
		}
	}
	float CalculatePreviewCameraDistance(const Engine::Vector3& min, const Engine::Vector3& max,
		const Engine::Vector3& center, float pitchDegrees, float yawDegrees, float fovYDegrees,
		float aspectRatio, float distanceScale) {

		const float halfFovY = (fovYDegrees * 0.5f) * 3.1415926535f / 180.0f;
		const float tanY = (std::max)(std::tan(halfFovY), 0.001f);
		const float tanX = (std::max)(tanY * (std::max)(aspectRatio, 0.001f), 0.001f);

		const Engine::Matrix4x4 rotation = Engine::Matrix4x4::MakeRotateMatrix(
			Engine::Vector3(pitchDegrees, yawDegrees, 0.0f));
		const Engine::Matrix4x4 inverseRotation = Engine::Matrix4x4::Inverse(rotation);

		float requiredDistance = 0.1f;
		for (int32_t ix = 0; ix < 2; ++ix) {
			for (int32_t iy = 0; iy < 2; ++iy) {
				for (int32_t iz = 0; iz < 2; ++iz) {

					const Engine::Vector3 corner(
						ix == 0 ? min.x : max.x,
						iy == 0 ? min.y : max.y,
						iz == 0 ? min.z : max.z);
					const Engine::Vector3 local = Engine::Vector3::TransferNormal(corner - center, inverseRotation);
					requiredDistance = (std::max)(requiredDistance, std::fabs(local.x) / tanX - local.z);
					requiredDistance = (std::max)(requiredDistance, std::fabs(local.y) / tanY - local.z);
				}
			}
		}
		return (std::max)(requiredDistance, 0.1f) * (std::max)(distanceScale, 1.0f) * 1.08f;
	}
	void ImportModelReferencedTextures(Engine::AssetDatabase& database, Engine::AssetID meshAssetID) {

		if (!meshAssetID) {
			return;
		}

		const std::filesystem::path fullPath = database.ResolveFullPath(meshAssetID);
		if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
			return;
		}

		Engine::TextureAssetResolver textureResolver{};
		textureResolver.Build(fullPath);

		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(fullPath.string(), kModelPreviewAssimpFlags);
		if (!scene || scene->mNumMaterials == 0) {
			return;
		}

		auto importTexture = [&](const std::string& reference) {

			const std::string assetPath = textureResolver.ResolveAssetPath(reference);
			if (!assetPath.empty()) {

				database.ImportOrGet(assetPath, Engine::AssetType::Texture);
			}
			};

		for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {

			aiMaterial* material = scene->mMaterials[materialIndex];
			importTexture(GetMaterialTextureReference(material, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }));
			importTexture(GetMaterialTextureReference(material, { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT }));
			importTexture(GetMaterialTextureReference(material, { aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN }));
			importTexture(GetMaterialTextureReference(material, { aiTextureType_SPECULAR }));
			importTexture(GetMaterialTextureReference(material, { aiTextureType_EMISSIVE, aiTextureType_EMISSION_COLOR }));
			importTexture(GetMaterialTextureReference(material, { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP }));
		}
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

	modelPreviewCameraController_ = std::make_unique<SceneViewCameraController>();
	modelPreviewCameraController_->MakeDefaultState();
	modelPreviewCameraController_->SetSavePath(RuntimePaths::GetEngineAssetPath(
		"Config/inspectorModelPreviewCamera.exeConfig.json").string());

	// コンポーネント描画の登録
	componentDrawers_.emplace_back(std::make_unique<TransformInspectorDrawer>());
	componentDrawers_.emplace_back(std::make_unique<OrthographicCameraInspectorDrawer>());
	componentDrawers_.emplace_back(std::make_unique<PerspectiveCameraInspectorDrawer>());
	componentDrawers_.emplace_back(std::make_unique<CameraControllerInspectorDrawer>());
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
	componentDrawers_.emplace_back(std::make_unique<AudioSourceInspectorDrawer>());
	componentDrawers_.emplace_back(std::make_unique<CollisionInspectorDrawer>());
	componentDrawers_.emplace_back(std::make_unique<ScriptInspectorDrawer>());
}

void Engine::InspectorPanel::DrawEditorTool([[maybe_unused]] const EditorToolContext& context) {

	// InspectorPanelはToolPanel上の独立ウィンドウを持たず、RenderTexture作成機能だけを利用する。
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
	ImportModelReferencedTextures(*database, meta.guid);

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
	renderer.queue = "Opaque";
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
	if (!database || !meta.guid) {
		return bounds;
	}

	const std::filesystem::path fullPath = database->ResolveFullPath(meta.guid);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		return bounds;
	}

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(fullPath.string(), kModelPreviewAssimpFlags);
	if (!scene || !scene->HasMeshes()) {
		return bounds;
	}

	std::vector<Vector3> positions{};
	CollectAssimpNodePositions(scene, scene->mRootNode, aiMatrix4x4(), positions);
	if (positions.empty()) {
		return bounds;
	}

	Vector3 minV(FLT_MAX, FLT_MAX, FLT_MAX);
	Vector3 maxV(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (const Vector3& p : positions) {

		minV.x = (std::min)(minV.x, p.x);
		minV.y = (std::min)(minV.y, p.y);
		minV.z = (std::min)(minV.z, p.z);
		maxV.x = (std::max)(maxV.x, p.x);
		maxV.y = (std::max)(maxV.y, p.y);
		maxV.z = (std::max)(maxV.z, p.z);
	}

	bounds.min = minV;
	bounds.max = maxV;
	bounds.center = (minV + maxV) * 0.5f;

	float radius = 0.0f;
	for (const Vector3& p : positions) {

		radius = (std::max)(radius, (p - bounds.center).Length());
	}
	bounds.radius = (std::max)(radius, 0.1f);
	bounds.valid = true;
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
		CalculatePreviewCameraDistance(modelPreviewBounds_.min, modelPreviewBounds_.max, center,
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
