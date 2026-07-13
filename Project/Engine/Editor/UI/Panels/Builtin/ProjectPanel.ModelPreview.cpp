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

#include <Engine/Core/Rendering/Meshes/Import/AssimpMaterialTextureExtractor.h>
#include <Engine/Editor/Assets/Preview/ModelPreviewUtility.h>

//============================================================================
//	ProjectPanel modelPreview classMethods
//	モデルサムネイルプレビューのatlas生成と描画
//============================================================================
namespace {

	constexpr const char* kProjectModelPreviewAtlasName = "ProjectPanelModelPreviewAtlas";
	constexpr uint32_t kModelPreviewColorTargetCount = 3;
	constexpr uint32_t kModelPreviewRefreshFrameCount = 30;

	void HashCombine(uint64_t& seed, uint64_t value) {

		seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
	}
	void HashString(uint64_t& seed, const std::string& value) {

		for (char c : value) {
			HashCombine(seed, static_cast<uint8_t>(c));
		}
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
		modelPreviewWorld_.reset();
		modelPreviewSlots_.clear();
		modelPreviewSlotByAsset_.clear();
		modelPreviewSignature_ = 0;
		modelPreviewDirectory_ = node.virtualPath;
		modelPreviewLightEntity_ = Entity::Null();
		modelPreviewAtlasSize_.Init();
		modelPreviewRefreshFrames_ = 0;
		DestroyRenderTexture(kProjectModelPreviewAtlasName);
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
		atlas->size.y != modelPreviewAtlasSize_.y)) {

		DestroyRenderTexture(kProjectModelPreviewAtlasName);
		atlas = nullptr;
		modelPreviewRefreshFrames_ = kModelPreviewRefreshFrameCount;
	}
	if (!atlas) {
		modelPreviewRefreshFrames_ = kModelPreviewRefreshFrameCount;
		atlas = CreateRenderTexture(kProjectModelPreviewAtlasName,
			modelPreviewAtlasSize_, modelPreviewSettings_.clearColor, kModelPreviewColorTargetCount);
	}
	if (atlas && modelPreviewRefreshFrames_ > 0) {
		RenderModelPreviewAtlas(toolContext, *atlas);
		--modelPreviewRefreshFrames_;
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
	modelPreviewRefreshFrames_ = kModelPreviewRefreshFrameCount;

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
		ModelPreviewUtility::ImportReferencedTextures(database, asset.assetID);

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

		std::error_code ec{};
		const auto writeTime = std::filesystem::last_write_time(RuntimePaths::ResolveAssetPath(asset->assetPath), ec);
		if (!ec) {
			HashCombine(signature, static_cast<uint64_t>(writeTime.time_since_epoch().count()));
		}
	}
	return signature;
}

Engine::ProjectPanel::ModelPreviewBounds Engine::ProjectPanel::ComputeModelPreviewBounds(
	AssetDatabase& database, AssetID meshAssetID) const {

	ModelPreviewBounds bounds{};
	bounds.valid = ModelPreviewUtility::ComputeBounds(database, meshAssetID,
		bounds.min, bounds.max, bounds.center, bounds.radius);
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
		ModelPreviewUtility::CalculateCameraDistance(bounds.min, bounds.max, center, pitchDegrees, yawDegrees, fovY,
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

