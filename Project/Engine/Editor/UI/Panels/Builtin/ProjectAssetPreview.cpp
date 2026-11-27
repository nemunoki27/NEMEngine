#include "ProjectAssetPreview.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/EditorPalette.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Editor/Assets/Preview/ModelPreviewUtility.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {

	constexpr size_t kCacheCapacity = 64;
	constexpr int32_t kThumbnailSize = 192;


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

void Engine::ProjectAssetPreview::Request(AssetID asset) {

	if (std::find(visibleAssets_.begin(), visibleAssets_.end(), asset) == visibleAssets_.end()) {
		visibleAssets_.push_back(asset);
	}
}

const char* Engine::ProjectAssetPreview::GetFallbackLabel(AssetID asset) const {

	const auto found = entries_.find(asset);
	return found != entries_.end() && found->second.fallback ? found->second.fallback : "";
}

std::string Engine::ProjectAssetPreview::MakeTextureName(AssetID asset) {

	return "ProjectAssetPreview:" + ToString(asset);
}

void Engine::ProjectAssetPreview::Reset(const AssetDatabase& database) {

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

void Engine::ProjectAssetPreview::TrimCache() {

	while (entries_.size() > kCacheCapacity) {
		const auto oldest = std::min_element(entries_.begin(), entries_.end(), [](const auto& left, const auto& right) {
			return left.second.lastUsedFrame < right.second.lastUsedFrame;
		});
		// 現在表示中の項目は追い出さない
		if (oldest->second.lastUsedFrame == frame_) {
			break;
		}
		resources_.DestroyRenderTexture(MakeTextureName(oldest->first));
		if (world_->IsAlive(oldest->second.entity)) world_->DestroyEntity(oldest->second.entity);
		entries_.erase(oldest);
	}
}

bool Engine::ProjectAssetPreview::UpdateTextureReferences(PreviewEntry& entry, TextureUploadService& uploads, bool update) {

	bool changed = false;
	for (auto& [key, previous] : entry.textures) {
		const GPUTextureResource* texture = uploads.GetTexture(key);
		const std::weak_ptr<const uint8_t> current = texture && texture->valid ? texture->ownership : TextureOwnership{};
		changed |= previous.owner_before(current) || current.owner_before(previous);
		if (update) {
			previous = current;
		}
	}
	return changed;
}

void Engine::ProjectAssetPreview::CollectMaterialDependencies(PreviewEntry& entry, const AssetDatabase& database, AssetID asset) {

	entry.textures.clear();
	entry.dependencies.clear();
	std::vector<AssetID> pending{asset};
	std::unordered_set<AssetID> visited;
	while (!pending.empty()) {
		const AssetID id = pending.back();
		pending.pop_back();
		const auto* dependency = database.Find(id);
		if (!dependency || !visited.insert(id).second) continue;
		entry.dependencies.emplace_back(id, database.GetContentRevision(id));
		if (dependency->type == AssetType::Texture) {
			for (auto& key : RuntimeTextureResolver::GetTextureCacheKeys(database, id)) {
				entry.textures.emplace_back(std::move(key), TextureOwnership{});
			}
		}
		pending.insert(pending.end(), dependency->dependencies.begin(), dependency->dependencies.end());
	}
}

void Engine::ProjectAssetPreview::Prepare(const EditorPanelContext& context, AssetDatabase& database) {

	if (visibleAssets_.empty() || !context.graphicsCore || !context.renderPipeline) {
		return;
	}
	if (!world_ || databaseLifetime_.lock() != database.GetCacheLifetime().lock()) {
		Reset(database);
	}
	++frame_;
	std::vector<AssetID> assets;
	assets.swap(visibleAssets_);
	// 重いモデル解析は既存workerへ渡し、完了待ちは行わない
	std::vector<AssetID> meshes;
	for (const AssetID asset : assets) {
		const auto* meta = database.Find(asset);
		if (!meta) continue;
		const auto [found, inserted] = entries_.try_emplace(asset);
		auto& entry = found->second;
		entry.lastUsedFrame = frame_;
		if (meta->type != AssetType::Mesh) continue;
		meshes.push_back(asset);
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
	if (!meshes.empty()) context.renderPipeline->PreparePreviewMeshes(*context.graphicsCore, database, meshes);
	TrimCache();
	if (assets.empty()) {
		return;
	}
	EditorToolContext toolContext{};
	toolContext.panelContext = &context;
	toolContext.toolContext.assetDatabase = &database;
	resources_.BeginEditorToolFrame(toolContext);
	const Color4 background = EditorPalette::RGBA("Panels.Model.projectBackground");
	if (thumbnailBackground_ != background) {
		thumbnailBackground_ = background;
		for (auto& [asset, entry] : entries_) {
			resources_.DestroyRenderTexture(MakeTextureName(asset));
			entry.rendered = false;
		}
	}
	const uint64_t textureRevision = context.graphicsCore->GetTextureUploadService().GetContentRevision();
	textureQuietFrames_ = observedTextureRevision_ == textureRevision ? (std::min)(textureQuietFrames_ + 1, 8u) : 0;
	observedTextureRevision_ = textureRevision;
	// 1frameに1アセットだけ描画し、完成画像はフォルダー切替後も保持する
	for (size_t index = 0; index < assets.size(); ++index) {
		const size_t cursor = (nextEntry_ + index) % assets.size();
		const AssetID asset = assets[cursor];
		const auto* meta = database.Find(asset);
		if (!meta) continue;
		auto& entry = entries_.at(asset);
		if (meta->type == AssetType::Material) {
			if (entry.databaseRevision != database.GetContentRevision()) {
				entry.databaseRevision = database.GetContentRevision();
				for (const auto& [id, revision] : entry.dependencies) {
					if (!database.Find(id) || revision != database.GetContentRevision(id)) entry.rendered = false;
				}
			}
			if (entry.rendered && entry.textureRevision == textureRevision) continue;
			if (entry.rendered && !UpdateTextureReferences(entry, context.graphicsCore->GetTextureUploadService(), false)) {
				entry.textureRevision = textureRevision;
				continue;
			}
			if (frame_ < entry.retryFrame) continue;
			entry.retryFrame = frame_ + 12;
			const auto* material = context.renderPipeline->GetRenderAssetLibrary().LoadMaterial(asset);
			if (!material) {
				entry.fallback = "読込失敗";
				continue;
			}
			// 専用バッファを前提にしたエンジンの画面処理は単独で実行しない
			entry.fallback = nullptr;
			if (material->domain == MaterialDomain::Fullscreen) entry.fallback = "Fullscreen";
			if (material->domain == MaterialDomain::Compute && !material->shaderGraph) entry.fallback = "Compute";
			if (material->domain == MaterialDomain::RayTracing &&
				(!material->shaderGraph || !context.graphicsCore->GetDXObject().ShouldUseDispatchRays())) entry.fallback = "Ray Tracing";
			if (entry.fallback) {
				entry.retryFrame = frame_ + 60;
				continue;
			}
			auto* output = resources_.CreateRenderTexture(MakeTextureName(asset), {kThumbnailSize, kThumbnailSize},
				background, 1, true, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
			if (!output) continue;
			entry.rendered = materialPreview_.RenderThumbnail(context, *material, *output, background);
			if (entry.rendered) {
				entry.retryFrame = 0;
				CollectMaterialDependencies(entry, database, asset);
				UpdateTextureReferences(entry, context.graphicsCore->GetTextureUploadService());
				entry.textureRevision = textureRevision;
				entry.databaseRevision = database.GetContentRevision();
			}
			nextEntry_ = cursor + 1;
			break;
		}
		const MeshGPUResource* mesh = context.renderPipeline->FindPreviewMesh(asset);
		if (!mesh || (entry.rendered && entry.meshGeneration == mesh->reloadGeneration &&
			(entry.textureRevision == textureRevision || textureQuietFrames_ < 8))) {
			continue;
		}
		if (entry.rendered && entry.meshGeneration == mesh->reloadGeneration &&
			!UpdateTextureReferences(entry, context.graphicsCore->GetTextureUploadService(), false)) {
			entry.textureRevision = textureRevision;
			continue;
		}
		// 初回とMesh更新時だけプレビューのMaterialを同期する
		if (!entry.rendered || entry.meshGeneration != mesh->reloadGeneration) {
			const auto materials = ModelPreviewUtility::BuildMaterials(mesh->subMeshes);
			SetMeshSubMeshes(*world_, entry.entity, materials);
			entry.textures.clear();
			for (const auto& subMesh : mesh->subMeshes) {
				const AssetID textureAsset = subMesh.defaultTextureAssets.baseColorTexture;
				if (textureAsset) {
					entry.textures.emplace_back(RuntimeTextureResolver::MakeThumbnailKey(
						Algorithm::PathToUTF8(database.ResolveFullPath(textureAsset))), TextureOwnership{});
				}
			}
		}
		auto* texture = resources_.CreateRenderTexture(MakeTextureName(asset), Vector2I(kThumbnailSize, kThumbnailSize), background, 3);
		if (!texture) {
			continue;
		}
		texture->clearColor = background;
		bool rendered = false;
		resources_.RenderToTexture(*texture, [&](EditorToolRenderContext& renderContext) {
			EntityPreviewRenderRequest request{};
			request.useThumbnailTextures = true;
			request.world = world_.get();
			request.assetDatabase = &database;
			request.rootEntity = entry.entity;
			request.surface = texture->GetRenderTarget();
			request.camera = MakeCamera(*mesh);
			request.clearColor = background;
			rendered = context.renderPipeline->RenderEntityPreview(*renderContext.graphicsCore, request);
		}, background);
		entry.rendered = rendered;
		entry.meshGeneration = mesh->reloadGeneration;
		entry.textureRevision = textureRevision;
		UpdateTextureReferences(entry, context.graphicsCore->GetTextureUploadService());
		nextEntry_ = cursor + 1;
		break;
	}
	resources_.EndEditorToolFrame();
}

bool Engine::ProjectAssetPreview::TryGetImage(AssetID assetID,
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
