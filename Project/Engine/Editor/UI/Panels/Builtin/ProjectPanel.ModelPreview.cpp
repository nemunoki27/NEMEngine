#include "ProjectPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Editor/Scripting/ManagedIdeLauncher.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Editor/Commands/Entity/InstantiatePrefabCommand.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// assimp
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

// windows
#include <windows.h>
#include <shellapi.h>

// c++
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <initializer_list>
#include <stack>
#include <string>
#include <system_error>
#include <vector>

#include <Engine/Editor/Assets/Importer/Model/AssimpMaterialTextureExtractor.h>

//============================================================================
//	ProjectPanel modelPreview classMethods
//	モデルサムネイルプレビューのatlas生成と描画でProjectPanel本体から分割
//============================================================================
namespace {

	constexpr const char* kProjectModelPreviewAtlasName = "ProjectPanelModelPreviewAtlas";
	constexpr uint32_t kModelPreviewColorTargetCount = 3;
	constexpr uint32_t kModelPreviewAssimpFlags =
		aiProcess_FlipWindingOrder |
		aiProcess_FlipUVs |
		aiProcess_Triangulate |
		aiProcess_GenSmoothNormals |
		aiProcess_CalcTangentSpace |
		aiProcess_JoinIdenticalVertices |
		aiProcess_ImproveCacheLocality |
		aiProcess_SortByPType;

	void HashCombine(uint64_t& seed, uint64_t value) {

		seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
	}
	void HashString(uint64_t& seed, const std::string& value) {

		for (char c : value) {
			HashCombine(seed, static_cast<uint8_t>(c));
		}
	}
	Engine::Vector3 ToEnginePreviewPosition(const aiVector3D& pos) {

		return Engine::Vector3(-pos.x, pos.y, pos.z);
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
			importTexture(Engine::AssimpMaterialTextureExtractor::Extract(material, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }));
			importTexture(Engine::AssimpMaterialTextureExtractor::Extract(material, { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT }));
			importTexture(Engine::AssimpMaterialTextureExtractor::Extract(material, { aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN }));
			importTexture(Engine::AssimpMaterialTextureExtractor::Extract(material, { aiTextureType_SPECULAR }));
			importTexture(Engine::AssimpMaterialTextureExtractor::Extract(material, { aiTextureType_EMISSIVE, aiTextureType_EMISSION_COLOR }));
			importTexture(Engine::AssimpMaterialTextureExtractor::Extract(material, { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP }));
		}
	}
}

void Engine::ProjectPanel::DrawModelPreviewSettingsWindow() {

	const bool wasOpen = showModelPreviewSettingsWindow_;
	ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("モデルプレビュー設定", &showModelPreviewSettingsWindow_)) {

		ImGui::End();
		if (wasOpen && !showModelPreviewSettingsWindow_) {

			SavePersistentState();
		}
		return;
	}

	bool changed = false;
	bool saveRequested = false;
	bool atlasRebuildRequested = false;
	auto consumeEditState = [&](bool valueChanged, bool rebuildAtlasOnCommit) {

		changed |= valueChanged;
		if (ImGui::IsItemDeactivatedAfterEdit()) {

			saveRequested = true;
			atlasRebuildRequested |= rebuildAtlasOnCommit;
		}
	};

	ImGui::TextUnformatted("アトラス");
	consumeEditState(ImGui::DragInt("スロットサイズ", &modelPreviewSettings_.tileSize, 1.0f, 64, 512), true);

	float clearColor[4] = {
		modelPreviewSettings_.clearColor.r,
		modelPreviewSettings_.clearColor.g,
		modelPreviewSettings_.clearColor.b,
		modelPreviewSettings_.clearColor.a,
	};
	const bool clearColorChanged = ImGui::ColorEdit4("背景色", clearColor);
	if (clearColorChanged) {

		modelPreviewSettings_.clearColor = Color4(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
	}
	consumeEditState(clearColorChanged, true);

	ImGui::Separator();
	ImGui::TextUnformatted("カメラ");
	consumeEditState(ImGui::DragFloat("視野角", &modelPreviewSettings_.cameraFovY, 0.1f, 10.0f, 80.0f, "%.1f"), false);
	consumeEditState(ImGui::DragFloat("距離倍率", &modelPreviewSettings_.cameraDistanceScale, 0.01f, 0.5f, 6.0f, "%.2f"), false);
	consumeEditState(ImGui::DragFloat("ピッチ角", &modelPreviewSettings_.cameraPitchDegrees, 0.1f, -45.0f, 45.0f, "%.1f"), false);
	consumeEditState(ImGui::DragFloat("ヨー角", &modelPreviewSettings_.cameraYawDegrees, 0.1f, -360.0f, 360.0f, "%.1f"), false);

	ImGui::Separator();
	ImGui::TextUnformatted("ライト");
	float lightDirection[3] = {
		modelPreviewSettings_.lightDirection.x,
		modelPreviewSettings_.lightDirection.y,
		modelPreviewSettings_.lightDirection.z,
	};
	const bool lightDirectionChanged = ImGui::DragFloat3("ライト方向", lightDirection, 0.01f, -1.0f, 1.0f, "%.2f");
	if (lightDirectionChanged) {

		modelPreviewSettings_.lightDirection = Vector3(lightDirection[0], lightDirection[1], lightDirection[2]);
	}
	consumeEditState(lightDirectionChanged, false);
	consumeEditState(ImGui::DragFloat("ライト強度", &modelPreviewSettings_.lightIntensity, 0.01f, 0.0f, 10.0f, "%.2f"), false);

	ImGui::Separator();
	if (ImGui::Button("初期値に戻す")) {

		modelPreviewSettings_ = ModelPreviewAppearanceSettings{};
		changed = true;
		saveRequested = true;
		atlasRebuildRequested = true;
	}

	if (changed) {

		ClampModelPreviewSettings();
	}
	if (atlasRebuildRequested) {

		InvalidateModelPreviewAtlas();
	}
	if (saveRequested) {

		SavePersistentState();
	}

	ImGui::End();
	if (wasOpen && !showModelPreviewSettingsWindow_) {

		SavePersistentState();
	}
}

void Engine::ProjectPanel::ClampModelPreviewSettings() {

	modelPreviewSettings_.tileSize = std::clamp(modelPreviewSettings_.tileSize, 64, 512);
	modelPreviewSettings_.cameraFovY = std::clamp(modelPreviewSettings_.cameraFovY, 10.0f, 80.0f);
	modelPreviewSettings_.cameraDistanceScale = std::clamp(modelPreviewSettings_.cameraDistanceScale, 0.5f, 6.0f);
	modelPreviewSettings_.cameraPitchDegrees = std::clamp(modelPreviewSettings_.cameraPitchDegrees, -45.0f, 45.0f);
	modelPreviewSettings_.lightIntensity = std::clamp(modelPreviewSettings_.lightIntensity, 0.0f, 10.0f);
	if (modelPreviewSettings_.lightDirection.Length() <= 0.001f) {

		modelPreviewSettings_.lightDirection = ModelPreviewAppearanceSettings{}.lightDirection;
	}
}

void Engine::ProjectPanel::ApplyModelPreviewLightSettings() {

	if (!modelPreviewWorld_ || !modelPreviewWorld_->IsAlive(modelPreviewLightEntity_)) {
		return;
	}

	DirectionalLightComponent* light = modelPreviewWorld_->TryGetComponent<DirectionalLightComponent>(modelPreviewLightEntity_);
	if (!light) {
		return;
	}

	light->direction = modelPreviewSettings_.lightDirection.Normalize();
	light->intensity = modelPreviewSettings_.lightIntensity;
}

void Engine::ProjectPanel::InvalidateModelPreviewAtlas() {

	modelPreviewWorld_.reset();
	modelPreviewLightEntity_ = Entity::Null();
	modelPreviewSlots_.clear();
	modelPreviewSlotByAsset_.clear();
	modelPreviewDirectory_.clear();
	modelPreviewSignature_ = 0;
	modelPreviewAtlasSize_ = Vector2I{};
	modelPreviewAtlasRebuildRequested_ = true;
}

void Engine::ProjectPanel::PrepareModelPreviewAtlas(const EditorPanelContext& context,
	AssetDatabase& database, const ProjectDirectoryNode& node) {

	std::vector<const ProjectAssetEntry*> meshAssets{};
	meshAssets.reserve(node.assets.size());
	for (const auto& asset : node.assets) {
		if (asset.type == AssetType::Mesh) {
			meshAssets.emplace_back(&asset);
		}
	}

	if (meshAssets.empty()) {
		modelPreviewSlots_.clear();
		modelPreviewSlotByAsset_.clear();
		modelPreviewSignature_ = 0;
		modelPreviewDirectory_ = node.virtualPath;
		modelPreviewLightEntity_ = Entity::Null();
		return;
	}

	const uint64_t signature = BuildModelPreviewSignature(node, meshAssets);
	if (signature != modelPreviewSignature_ || modelPreviewDirectory_ != node.virtualPath) {
		RebuildModelPreviewSlots(database, node, meshAssets, signature);
	}

	if (!context.graphicsCore || !context.renderPipeline || !modelPreviewWorld_ ||
		modelPreviewSlots_.empty() || modelPreviewAtlasSize_.x <= 0 || modelPreviewAtlasSize_.y <= 0) {
		return;
	}

	EditorToolContext toolContext{};
	toolContext.panelContext = &context;
	toolContext.toolContext.world = context.editorContext ? context.editorContext->activeWorld : nullptr;
	toolContext.toolContext.assetDatabase = &database;
	toolContext.toolContext.sceneInstances = context.editorContext ? context.editorContext->sceneInstances : nullptr;
	toolContext.toolContext.activeSceneHeader = context.editorContext ? context.editorContext->activeSceneHeader : nullptr;
	toolContext.toolContext.activeSceneAsset = context.editorContext ? context.editorContext->activeSceneAsset : AssetID{};
	toolContext.toolContext.activeSceneInstanceID = context.editorContext ? context.editorContext->activeSceneInstanceID : UUID{};
	if (context.editorContext) {
		toolContext.toolContext.activeScenePath = context.editorContext->activeScenePath;
	}
	toolContext.toolContext.isPlaying = context.IsPlaying();
	toolContext.toolContext.canEditScene = context.CanEditScene();

	BeginEditorToolFrame(toolContext);

	EditorToolRenderTexture* atlas = FindRenderTexture(kProjectModelPreviewAtlasName);
	if (atlas && (atlas->size.x != modelPreviewAtlasSize_.x ||
		atlas->size.y != modelPreviewAtlasSize_.y ||
		(modelPreviewAtlasRebuildRequested_ && atlas->clearColor != modelPreviewSettings_.clearColor))) {

		DestroyRenderTexture(kProjectModelPreviewAtlasName);
		atlas = nullptr;
	}
	if (!atlas) {
		atlas = CreateRenderTexture(kProjectModelPreviewAtlasName,
			modelPreviewAtlasSize_, modelPreviewSettings_.clearColor, kModelPreviewColorTargetCount);
	}
	if (atlas) {
		RenderModelPreviewAtlas(toolContext, *atlas);
		modelPreviewAtlasRebuildRequested_ = false;
	}

	EndEditorToolFrame();
}

void Engine::ProjectPanel::RebuildModelPreviewSlots(AssetDatabase& database, const ProjectDirectoryNode& node,
	const std::vector<const ProjectAssetEntry*>& meshAssets, uint64_t signature) {

	modelPreviewWorld_ = std::make_unique<ECSWorld>();
	modelPreviewSlots_.clear();
	modelPreviewSlotByAsset_.clear();
	modelPreviewDirectory_ = node.virtualPath;
	modelPreviewSignature_ = signature;

	const int32_t count = static_cast<int32_t>(meshAssets.size());
	const int32_t columns = (std::max)(1, static_cast<int32_t>(std::ceil(std::sqrt(static_cast<float>(count)))));
	const int32_t rows = (std::max)(1, (count + columns - 1) / columns);
	const int32_t tileSize = modelPreviewSettings_.tileSize;
	modelPreviewAtlasSize_ = Vector2I(columns * tileSize, rows * tileSize);

	Entity lightEntity = modelPreviewWorld_->CreateEntity(UUID::New());
	auto& lightTransform = modelPreviewWorld_->AddComponent<TransformComponent>(lightEntity);
	lightTransform.worldMatrix = Matrix4x4::Identity();
	lightTransform.isDirty = false;
	auto& light = modelPreviewWorld_->AddComponent<DirectionalLightComponent>(lightEntity);
	light.direction = modelPreviewSettings_.lightDirection.Normalize();
	light.intensity = modelPreviewSettings_.lightIntensity;
	modelPreviewLightEntity_ = lightEntity;

	modelPreviewSlots_.reserve(meshAssets.size());
	for (int32_t i = 0; i < count; ++i) {
		const ProjectAssetEntry& asset = *meshAssets[static_cast<size_t>(i)];
		ImportModelReferencedTextures(database, asset.assetID);

		Entity entity = modelPreviewWorld_->CreateEntity(UUID::New());
		auto& transform = modelPreviewWorld_->AddComponent<TransformComponent>(entity);
		transform.worldMatrix = Matrix4x4::Identity();
		transform.isDirty = false;

		auto& renderer = modelPreviewWorld_->AddComponent<MeshRendererComponent>(entity);
		renderer.mesh = asset.assetID;
		renderer.material = {};
		renderer.queue = RenderPhase::Opaque;
		renderer.visible = true;
		renderer.enableZPrepass = true;
		MeshSubMeshAuthoring::SyncComponent(&database, renderer, false);

		const int32_t column = i % columns;
		const int32_t row = i / columns;
		const Vector2I pixelPos(column * tileSize, row * tileSize);
		const Vector2I pixelSize(tileSize, tileSize);

		ModelPreviewSlot slot{};
		slot.assetID = asset.assetID;
		slot.assetPath = asset.assetPath;
		slot.entity = entity;
		slot.pixelPos = pixelPos;
		slot.pixelSize = pixelSize;
		slot.uv0 = ImVec2(
			static_cast<float>(pixelPos.x) / static_cast<float>(modelPreviewAtlasSize_.x),
			static_cast<float>(pixelPos.y) / static_cast<float>(modelPreviewAtlasSize_.y));
		slot.uv1 = ImVec2(
			static_cast<float>(pixelPos.x + pixelSize.x) / static_cast<float>(modelPreviewAtlasSize_.x),
			static_cast<float>(pixelPos.y + pixelSize.y) / static_cast<float>(modelPreviewAtlasSize_.y));
		slot.bounds = ComputeModelPreviewBounds(database, asset.assetID);

		modelPreviewSlotByAsset_[slot.assetID] = modelPreviewSlots_.size();
		modelPreviewSlots_.emplace_back(std::move(slot));
	}
}

void Engine::ProjectPanel::RenderModelPreviewAtlas(const EditorToolContext& toolContext,
	EditorToolRenderTexture& atlas) {

	if (!toolContext.panelContext || !toolContext.panelContext->renderPipeline || !modelPreviewWorld_) {
		return;
	}

	ApplyModelPreviewLightSettings();
	RenderToTexture(atlas, [&](EditorToolRenderContext& renderContext) {

		for (const ModelPreviewSlot& slot : modelPreviewSlots_) {
			if (!modelPreviewWorld_->IsAlive(slot.entity)) {
				continue;
			}

			EntityPreviewRenderRequest request{};
			request.world = modelPreviewWorld_.get();
			request.systemContext = toolContext.toolContext.systemContext;
			request.assetDatabase = toolContext.toolContext.assetDatabase;
			request.sceneHeader = toolContext.toolContext.activeSceneHeader;
			request.sceneInstanceID = {};
			request.rootEntity = slot.entity;
			request.surface = atlas.GetRenderTarget();
			request.camera = BuildModelPreviewCamera(slot.bounds);
			request.clearColor = atlas.clearColor;
			request.clearSurface = false;
			request.useViewportRect = true;
			request.viewportX = static_cast<uint32_t>(slot.pixelPos.x);
			request.viewportY = static_cast<uint32_t>(slot.pixelPos.y);
			request.viewportWidth = static_cast<uint32_t>(slot.pixelSize.x);
			request.viewportHeight = static_cast<uint32_t>(slot.pixelSize.y);

			toolContext.panelContext->renderPipeline->RenderEntityPreview(*renderContext.graphicsCore, request);
		}
		}, atlas.clearColor);
}

bool Engine::ProjectPanel::TryGetModelPreviewImage(AssetID assetID,
	ImTextureID& outTextureID, ImVec2& outUV0, ImVec2& outUV1) const {

	const auto it = modelPreviewSlotByAsset_.find(assetID);
	if (it == modelPreviewSlotByAsset_.end() || modelPreviewSlots_.size() <= it->second) {
		return false;
	}

	const EditorToolRenderTexture* atlas = FindRenderTexture(kProjectModelPreviewAtlasName);
	if (!atlas || !atlas->IsValid()) {
		return false;
	}

	outTextureID = atlas->GetImTextureID();
	if (outTextureID == static_cast<ImTextureID>(0)) {
		return false;
	}

	const ModelPreviewSlot& slot = modelPreviewSlots_[it->second];
	outUV0 = slot.uv0;
	outUV1 = slot.uv1;
	return true;
}

uint64_t Engine::ProjectPanel::BuildModelPreviewSignature(const ProjectDirectoryNode& node,
	const std::vector<const ProjectAssetEntry*>& meshAssets) const {

	uint64_t signature = 1469598103934665603ull;
	HashString(signature, node.virtualPath);
	HashCombine(signature, static_cast<uint64_t>(meshAssets.size()));
	for (const ProjectAssetEntry* asset : meshAssets) {
		if (!asset) {
			continue;
		}
		HashCombine(signature, asset->assetID.value);
		HashString(signature, asset->assetPath);
	}
	return signature;
}

Engine::ProjectPanel::ModelPreviewBounds Engine::ProjectPanel::ComputeModelPreviewBounds(
	AssetDatabase& database, AssetID meshAssetID) const {

	ModelPreviewBounds bounds{};
	if (!meshAssetID) {
		return bounds;
	}

	const std::filesystem::path fullPath = database.ResolveFullPath(meshAssetID);
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

Engine::ManualRenderCameraState Engine::ProjectPanel::BuildModelPreviewCamera(
	const ModelPreviewBounds& bounds) const {

	const Vector3 center = bounds.valid ? bounds.center : Vector3::AnyInit(0.0f);
	const float radius = (std::max)(bounds.valid ? bounds.radius : 1.0f, 0.1f);
	const float fovY = modelPreviewSettings_.cameraFovY;
	const float pitchDegrees = modelPreviewSettings_.cameraPitchDegrees;
	const float yawDegrees = modelPreviewSettings_.cameraYawDegrees;
	const float distance = bounds.valid ?
		CalculatePreviewCameraDistance(bounds.min, bounds.max, center, pitchDegrees, yawDegrees, fovY,
			1.0f, modelPreviewSettings_.cameraDistanceScale) :
		radius * modelPreviewSettings_.cameraDistanceScale;
	const Matrix4x4 cameraRotation = Matrix4x4::MakeRotateMatrix(Vector3(pitchDegrees, yawDegrees, 0.0f));
	const Vector3 cameraForward(cameraRotation.m[2][0], cameraRotation.m[2][1], cameraRotation.m[2][2]);

	ManualRenderCameraState camera{};
	camera.enableOrthographic = false;
	camera.enablePerspective = true;
	camera.perspectiveFovY = fovY;
	camera.perspectiveNearClip = 0.01f;
	camera.perspectiveFarClip = (std::max)(10000.0f, distance + radius * 4.0f);
	camera.transform3D.pos = center - cameraForward * distance;
	camera.transform3D.rotation = Vector3(pitchDegrees, yawDegrees, 0.0f);
	return camera;
}

