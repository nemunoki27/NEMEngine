#include "MeshBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/Render/Backend/Common/BackendDrawCommon.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Animation/SkinnedAnimationComponent.h> 
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Logger/Assert.h>

//============================================================================
//	MeshBatchResources classMethods
//============================================================================

namespace {

	const Engine::MeshRendererComponent* ResolveRenderer(const Engine::RenderItem* item) {

		if (!item || !item->world) {
			return nullptr;
		}
		if (!item->world->IsAlive(item->entity)) {
			return nullptr;
		}
		if (!item->world->HasComponent<Engine::MeshRendererComponent>(item->entity)) {
			return nullptr;
		}
		return &item->world->GetComponent<Engine::MeshRendererComponent>(item->entity);
	}
	const Engine::SkinnedAnimationComponent* ResolveSkinnedAnimation(const Engine::RenderItem* item) {

		if (!item || !item->world) {
			return nullptr;
		}
		if (!item->world->IsAlive(item->entity)) {
			return nullptr;
		}
		if (!item->world->HasComponent<Engine::SkinnedAnimationComponent>(item->entity)) {
			return nullptr;
		}
		return &item->world->GetComponent<Engine::SkinnedAnimationComponent>(item->entity);
	}
}

void Engine::MeshBatchResources::Init(GraphicsCore& graphicsCore) {

	// すでに初期化されている場合は何もしない
	if (initialized_) {
		return;
	}

	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	SRVDescriptor* srvDescriptor = &graphicsCore.GetSRVDescriptor();

	// バッファ作成
	for (auto& viewBuffer : view_) {
		viewBuffer.Init(device);
	}
	meshData_.Init(device, srvDescriptor);
	psData_.Init(device, srvDescriptor);
	draw_.Init(device);
	subMeshData_.Init(device, srvDescriptor);

	meshData_.EnsureCapacity(256);
	psData_.EnsureCapacity(256);
	subMeshData_.EnsureCapacity(256);
	meshScratch_.reserve(256);
	psScratch_.reserve(256);
	subMeshScratch_.reserve(256);

	// 初期化完了
	initialized_ = true;
}

void Engine::MeshBatchResources::EnsureSkinningResources(GraphicsCore& graphicsCore) {

	// すでにスキニング用のリソースがある場合は何もしない
	if (skinning_) {
		return;
	}

	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	SRVDescriptor* srvDescriptor = &graphicsCore.GetSRVDescriptor();

	// スキニング用のリソースを作成する
	skinning_ = std::make_unique<OptionalSkinningResources>();
	skinning_->skinningPalette.Init(device, srvDescriptor);
	skinning_->skinnedVertices.Init(device, srvDescriptor);
	skinning_->skinningConstants.Init(device);

	skinning_->skinningPalette.EnsureCapacity(256);
	skinning_->skinnedVertices.EnsureCapacity(256);
	paletteScratch_.reserve(256);

	skinning_->skinnedVertexState = D3D12_RESOURCE_STATE_COMMON;
}

bool Engine::MeshBatchResources::FindSkinnedVertexOffset(ECSWorld* world, Entity entity, uint32_t& outVertexOffset) const {

	SkinnedEntityLookupKey key{};
	key.world = world;
	key.entity = entity;
	auto it = skinnedVertexOffsetMap_.find(key);
	if (it == skinnedVertexOffsetMap_.end()) {
		return false;
	}
	outVertexOffset = it->second;
	return true;
}

void Engine::MeshBatchResources::UpdateView(const ResolvedRenderView& view) {

	// 定数バッファにビュー行列を転送する
	MeshViewConstants constants{};
	if (const ResolvedCameraView* camera = view.FindCamera(RenderCameraDomain::Perspective)) {

		constants.viewProjection = camera->matrices.viewProjectionMatrix;
	}
	view_[ToViewIndex(view.kind)].Upload(constants);
}

void Engine::MeshBatchResources::UploadBatchData(const RenderDrawContext& drawContext,
	const RenderSceneBatch& batch, const std::span<const RenderItem* const>& items, const MeshGPUResource& gpuMesh) {

	// データクリア
	meshScratch_.clear();
	psScratch_.clear();
	subMeshScratch_.clear();
	paletteScratch_.clear();
	skinnedRecords_.clear();
	skinnedVertexOffsetMap_.clear();
	skinnedInstanceCount_ = 0;
	skinningDispatched_ = false;
	// 描画アイテム数に応じて必要なバッファサイズを確保する
	if (meshScratch_.capacity() < items.size()) {
		meshScratch_.reserve(items.size());
	}
	if (psScratch_.capacity() < items.size()) {
		psScratch_.reserve(items.size());
	}

	// サブメッシュデータはインスタンスごとに必要なため、アイテム数×サブメッシュ数の容量を確保する
	size_t totalSubMeshCount = items.size() * gpuMesh.subMeshes.size();
	if (subMeshScratch_.capacity() < totalSubMeshCount) {

		subMeshScratch_.reserve(totalSubMeshCount);
	}

	GraphicsCore& graphicsCore = *drawContext.graphicsCore;

	// スキニング可能メッシュのときだけリソース生成する
	if (gpuMesh.isSkinned) {

		EnsureSkinningResources(graphicsCore);
	}

	// エラーテクスチャのSRVインデックスを取得する
	const GPUTextureResource* fallback = graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
	uint32_t fallbackSRVIndex = (fallback && fallback->srvIndex != UINT32_MAX) ? fallback->srvIndex : 0;
	for (const RenderItem* item : items) {

		const MeshRenderPayload* payload = batch.GetPayload<MeshRenderPayload>(*item);
		if (!payload) {
			continue;
		}

		const MeshRendererComponent* renderer = ResolveRenderer(item);
		const SkinnedAnimationComponent* skinnedAnim = ResolveSkinnedAnimation(item);

		// MS/VS
		{
			MeshInstanceData instance{};
			instance.worldMatrix = item->worldMatrix;
			instance.subMeshDataOffset = static_cast<uint32_t>(subMeshScratch_.size());
			instance.subMeshCount = static_cast<uint32_t>(gpuMesh.subMeshes.size());

			// スキニングする場合の設定
			if (gpuMesh.isSkinned && skinning_ && skinnedAnim && skinnedAnim->runtimeInitialized &&
				skinnedAnim->palette.size() == gpuMesh.boneCount) {

				instance.flags |= kMeshInstanceFlagSkinned;
				instance.skinnedVertexOffset = skinnedInstanceCount_ * gpuMesh.vertexCount;

				// スキニングパレットデータを追加
				paletteScratch_.insert(paletteScratch_.end(), skinnedAnim->palette.begin(), skinnedAnim->palette.end());

				// スキニングするインスタンスのレコードを追加
				skinnedRecords_.push_back({ item->world,item->entity,instance.skinnedVertexOffset });
				SkinnedEntityLookupKey key{};
				key.world = item->world;
				key.entity = item->entity;
				skinnedVertexOffsetMap_[key] = instance.skinnedVertexOffset;

				// スキニングインスタンス数を加算
				++skinnedInstanceCount_;
			}
			meshScratch_.emplace_back(instance);
		}
		// PS
		{
			MeshPSInstanceData instance{};
			psScratch_.emplace_back(instance);
		}

		for (uint32_t subMeshIndex = 0; subMeshIndex < static_cast<uint32_t>(gpuMesh.subMeshes.size()); ++subMeshIndex) {

			// テクスチャアセットIDの取得
			AssetID baseColorTextureAsset = MeshDrawPathCommon::ResolveSubMeshBaseColorTextureAssetID(gpuMesh, renderer, subMeshIndex);
			const GPUTextureResource* texture = BackendDrawCommon::ResolveTextureAsset(drawContext, graphicsCore, baseColorTextureAsset);

			// サブメッシュデータの構築
			MeshSubMeshShaderData data{};
			data.baseColorTextureIndex = (texture && texture->srvIndex != UINT32_MAX) ? texture->srvIndex : fallbackSRVIndex;
			data.importedBaseColor = gpuMesh.subMeshes[subMeshIndex].baseColor;
			if (renderer && subMeshIndex < renderer->subMeshes.size()) {

				const auto& authoring = renderer->subMeshes[subMeshIndex];
				data.color = authoring.color;
				data.uvMatrix = authoring.uvMatrix;
				data.localMatrix = MeshSubMeshRuntime::BuildRenderLocalMatrix(authoring);
			}
			subMeshScratch_.emplace_back(data);
		}
	}

	// インスタンス数を設定
	instanceCount_ = static_cast<uint32_t>(meshScratch_.size());

	// GPUにデータ転送
	meshData_.Upload(meshScratch_);
	psData_.Upload(psScratch_);
	subMeshData_.Upload(subMeshScratch_);

	// 描画定数の転送
	MeshDrawConstants drawConstants{};
	drawConstants.meshletCount = gpuMesh.meshletCount;
	drawConstants.subMeshCount = static_cast<uint32_t>(gpuMesh.subMeshes.size());
	drawConstants.instanceCount = instanceCount_;
	draw_.Upload(drawConstants);

	if (skinning_) {

		// パレットデータの転送
		skinning_->skinningPalette.Upload(paletteScratch_);

		// スキニング定数の転送
		MeshSkinningDispatchConstants skinningConstants{};
		skinningConstants.vertexCount = gpuMesh.vertexCount;
		skinningConstants.boneCount = gpuMesh.boneCount;
		skinningConstants.skinnedInstanceCount = skinnedInstanceCount_;
		skinning_->skinningConstants.Upload(skinningConstants);

		// スキニング頂点バッファの容量を確保
		const uint32_t requiredSkinnedVertexCount = (std::max)(1u, skinnedInstanceCount_ * gpuMesh.vertexCount);
		const uint32_t prevCapacity = skinning_->skinnedVertices.GetCapacity();
		skinning_->skinnedVertices.EnsureCapacity(requiredSkinnedVertexCount);
		// スキニング頂点バッファの容量が変わった場合は、リソース状態をリセットする
		if (prevCapacity != skinning_->skinnedVertices.GetCapacity()) {

			skinning_->skinnedVertexState = D3D12_RESOURCE_STATE_COMMON;
		}
	}
}