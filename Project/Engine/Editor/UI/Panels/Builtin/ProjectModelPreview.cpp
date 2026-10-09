#include "ProjectModelPreview.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Editor/Assets/Preview/ModelPreviewUtility.h>

// c++
#include <algorithm>
#include <cmath>

namespace {

	constexpr size_t kCacheCapacity = 64;
	constexpr int32_t kThumbnailSize = 192;
	const Engine::Color4 kClearColor(0.04f, 0.06f, 0.16f, 1.0f);

	// 公開済みMeshの境界球へCameraを合わせる
	Engine::ManualRenderCameraState MakeCamera(const Engine::MeshGPUResource& mesh) {

		Engine::ManualRenderCameraState camera{};
		camera.enableOrthographic = false;
		camera.enablePerspective = true;
		camera.perspectiveFovY = 35.0f;
		camera.perspectiveNearClip = 0.01f;
		const float radius = (std::max)(0.1f, mesh.boundsRadius);
		const float distance = radius / std::sin(35.0f * 0.5f * 3.1415926535f / 180.0f) * 1.08f;
		camera.perspectiveFarClip = (std::max)(10000.0f, distance + radius * 4.0f);
		camera.transform3D.rotation = Engine::Vector3(25.0f, 215.0f, 0.0f);
		const auto rotation = Engine::Matrix4x4::MakeRotateMatrix(camera.transform3D.rotation);
		const Engine::Vector3 forward(rotation.m[2][0], rotation.m[2][1], rotation.m[2][2]);
		camera.transform3D.pos = mesh.boundsCenter - forward * distance;
		return camera;
	}
}

void Engine::ProjectModelPreview::RequestVisibleMesh(AssetID asset) {

	if (std::find(visibleAssets_.begin(), visibleAssets_.end(), asset) == visibleAssets_.end()) {
		visibleAssets_.push_back(asset);
	}
}

std::string Engine::ProjectModelPreview::MakeTextureName(AssetID asset) {

	return "ProjectModelPreview:" + ToString(asset);
}

void Engine::ProjectModelPreview::Reset(const AssetDatabase& database) {

	for (const auto& [asset, entry] : entries_) {
		resources_.DestroyRenderTexture(MakeTextureName(asset));
	}
	entries_.clear();
	world_ = std::make_unique<ECSWorld>();
	databaseLifetime_ = database.GetCacheLifetime();
	// Preview専用Worldのライトはモデル間で共有する
	const Entity light = world_->CreateEntity(UUID::New());
	world_->AddComponent<TransformComponent>(light).isDirty = false;
	auto& directional = world_->AddComponent<DirectionalLightComponent>(light);
	directional.direction = Vector3(0.35f, -0.65f, 0.65f).Normalize();
	directional.intensity = 1.5f;
}

void Engine::ProjectModelPreview::TrimCache() {

	while (entries_.size() > kCacheCapacity) {
		const auto oldest = std::min_element(entries_.begin(), entries_.end(), [](const auto& left, const auto& right) {
			return left.second.lastUsedFrame < right.second.lastUsedFrame;
		});
		// 現在表示中の項目は追い出さない
		if (oldest->second.lastUsedFrame == frame_) {
			break;
		}
		resources_.DestroyRenderTexture(MakeTextureName(oldest->first));
		world_->DestroyEntity(oldest->second.entity);
		entries_.erase(oldest);
	}
}

void Engine::ProjectModelPreview::PrepareModelPreviews(const EditorPanelContext& context, AssetDatabase& database) {

	if (!context.graphicsCore || !context.renderPipeline) {
		return;
	}
	if (!world_ || databaseLifetime_.lock() != database.GetCacheLifetime().lock()) {
		Reset(database);
	}
	++frame_;
	std::vector<AssetID> assets;
	assets.swap(visibleAssets_);
	// 重いモデル解析は既存workerへ渡し、完了待ちは行わない
	context.renderPipeline->PreparePreviewMeshes(*context.graphicsCore, database, assets);
	for (const AssetID asset : assets) {
		const auto [found, inserted] = entries_.try_emplace(asset);
		auto& entry = found->second;
		entry.lastUsedFrame = frame_;
		if (inserted) {
			entry.entity = world_->CreateEntity(UUID::New());
			auto& transform = world_->AddComponent<TransformComponent>(entry.entity);
			transform.worldMatrix = Matrix4x4::Identity();
			transform.isDirty = false;
			auto& renderer = world_->AddComponent<MeshRendererComponent>(entry.entity);
			renderer.mesh = asset;
			renderer.renderFlags = MeshRenderFlags::None;
		}
	}
	TrimCache();
	if (assets.empty()) {
		return;
	}
	EditorToolContext toolContext{};
	toolContext.panelContext = &context;
	toolContext.toolContext.assetDatabase = &database;
	resources_.BeginEditorToolFrame(toolContext);
	const uint64_t textureRevision = context.graphicsCore->GetTextureUploadService().GetContentRevision();
	textureQuietFrames_ = observedTextureRevision_ == textureRevision ? (std::min)(textureQuietFrames_ + 1, 8u) : 0;
	observedTextureRevision_ = textureRevision;
	// 1frameに1モデルだけ描画し、完成画像はフォルダー切替後も保持する
	for (size_t index = 0; index < assets.size(); ++index) {
		const size_t cursor = (nextEntry_ + index) % assets.size();
		const AssetID asset = assets[cursor];
		const MeshGPUResource* mesh = context.renderPipeline->FindPreviewMesh(asset);
		auto& entry = entries_.at(asset);
		if (!mesh || (entry.rendered && entry.meshGeneration == mesh->reloadGeneration &&
			(entry.textureRevision == textureRevision || textureQuietFrames_ < 8))) {
			continue;
		}
		// 初回とMesh更新時だけプレビューのMaterialを同期する
		if (!entry.rendered || entry.meshGeneration != mesh->reloadGeneration) {
			const auto materials = ModelPreviewUtility::BuildMaterials(mesh->subMeshes);
			SetMeshSubMeshes(*world_, entry.entity, materials);
		}
		auto* texture = resources_.CreateRenderTexture(MakeTextureName(asset), Vector2I(kThumbnailSize, kThumbnailSize), kClearColor, 3);
		if (!texture) {
			continue;
		}
		bool rendered = false;
		resources_.RenderToTexture(*texture, [&](EditorToolRenderContext& renderContext) {
			EntityPreviewRenderRequest request{};
			request.world = world_.get();
			request.assetDatabase = &database;
			request.rootEntity = entry.entity;
			request.surface = texture->GetRenderTarget();
			request.camera = MakeCamera(*mesh);
			request.clearColor = kClearColor;
			rendered = context.renderPipeline->RenderEntityPreview(*renderContext.graphicsCore, request);
		}, kClearColor);
		entry.rendered = rendered;
		entry.meshGeneration = mesh->reloadGeneration;
		entry.textureRevision = textureRevision;
		nextEntry_ = cursor + 1;
		break;
	}
	resources_.EndEditorToolFrame();
}

bool Engine::ProjectModelPreview::TryGetModelPreviewImage(AssetID assetID,
	ImTextureID& outTextureID, ImVec2& outUV0, ImVec2& outUV1) const {

	const auto found = entries_.find(assetID);
	if (found == entries_.end() || !found->second.rendered) {
		return false;
	}
	const auto* texture = resources_.FindRenderTexture(MakeTextureName(assetID));
	if (!texture || !texture->IsValid()) {
		return false;
	}
	outTextureID = texture->GetImTextureID();
	outUV0 = ImVec2(0.0f, 0.0f);
	outUV1 = ImVec2(1.0f, 1.0f);
	return true;
}
