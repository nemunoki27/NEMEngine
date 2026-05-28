#include "MeshBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/RHI/DirectX12/Common/D3D12Utils.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h> 
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

// c++
#include <unordered_map>

//============================================================================
//	MeshBatchResources classMethods
//============================================================================

namespace {

	bool CanCullView(const Engine::RenderDrawContext& drawContext, const Engine::MeshGPUResource& gpuMesh) {

		// スキニングメッシュはCPU側での静的Boundsがずれやすいため、ここでは安全側で除外する
		return drawContext.runtimeFeatures.useFrustumCulling &&
			drawContext.view && drawContext.cullingView && drawContext.cullingView->valid &&
			!gpuMesh.isSkinned;
	}

	const Engine::MeshRendererComponent* ResolveRenderer(const Engine::RenderItem* item) {

		if (!item || !item->world) {
			return nullptr;
		}
		return item->world->TryGetComponent<Engine::MeshRendererComponent>(item->entity);
	}
	const Engine::SkinnedAnimationComponent* ResolveSkinnedAnimation(const Engine::RenderItem* item) {

		if (!item || !item->world) {
			return nullptr;
		}
		return item->world->TryGetComponent<Engine::SkinnedAnimationComponent>(item->entity);
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
	// カリング後に残すインスタンスを書き込むRWバッファ
	visibleMeshData_.Init(device, srvDescriptor);
	draw_.Init(device);
	// ExecuteIndirect引数生成Computeに渡す固定Index数
	indirectArgs_.Init(device);
	subMeshData_.Init(device, srvDescriptor);
	DxUtils::CreateUavBufferResource(device, indexedIndirectArgs_, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));
	indexedIndirectArgsState_ = D3D12_RESOURCE_STATE_COMMON;

	// 初期値を大きめにして、カメラ移動時の細かい再確保を減らす
	meshData_.EnsureCapacity(256);
	visibleMeshData_.EnsureCapacity(256);
	subMeshData_.EnsureCapacity(256);
	meshScratch_.reserve(256);
	subMeshScratch_.reserve(256);

	// 初期化完了
	initialized_ = true;
}

void Engine::MeshBatchResources::UpdateIndexedIndirectArgsConstants(uint32_t indexCount) {

	// ComputeでDrawIndexedInstanced引数を組み立てるため、Index数だけCPUから渡す
	MeshIndirectArgsConstants constants{};
	constants.indexCount = indexCount;
	indirectArgs_.Upload(constants);
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
	// MeshShader経路のため、スキニング結果も圧縮頂点として保持する
	skinning_->skinnedPackedVertices.Init(device, srvDescriptor);
	skinning_->skinningConstants.Init(device);

	skinning_->skinningPalette.EnsureCapacity(256);
	skinning_->skinnedVertices.EnsureCapacity(256);
	skinning_->skinnedPackedVertices.EnsureCapacity(256);
	paletteScratch_.reserve(256);

	skinning_->skinnedVertexState = D3D12_RESOURCE_STATE_COMMON;
	skinning_->skinnedPackedVertexState = D3D12_RESOURCE_STATE_COMMON;
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

void Engine::MeshBatchResources::UpdateView(const ResolvedRenderView& view, const ResolvedRenderView* cullingView) {

	// 定数バッファにビュー行列を転送する
	MeshViewConstants constants{};
	if (const ResolvedCameraView* camera = view.FindCamera(RenderCameraDomain::Perspective)) {

		constants.viewProjection = camera->matrices.viewProjectionMatrix;
		constants.renderCameraPos = camera->cameraPos;
	}
	constants.viewSize = Vector2(static_cast<float>((std::max)(view.width, 1u)),
		static_cast<float>((std::max)(view.height, 1u)));
	const ResolvedRenderView* cullView = cullingView ? cullingView : &view;
	if (const ResolvedCameraView* camera = cullView->FindCamera(RenderCameraDomain::Perspective)) {

		// SceneViewでは描画行列とカリング行列が別になるため、両方をGPUへ渡す
		constants.cullingViewProjection = camera->matrices.viewProjectionMatrix;
		constants.cullingView = camera->matrices.viewMatrix;
		constants.cullingCameraPos = camera->cameraPos;
		constants.cullingNearClip = camera->nearClip;
		constants.cullingViewSize = Vector2(static_cast<float>((std::max)(cullView->width, 1u)),
			static_cast<float>((std::max)(cullView->height, 1u)));
		constants.cullingProjectionScale = Vector2(
			std::abs(camera->matrices.projectionMatrix.m[0][0]),
			std::abs(camera->matrices.projectionMatrix.m[1][1]));
	} else {
		constants.cullingViewProjection = constants.viewProjection;
		constants.cullingView = Matrix4x4::Identity();
		constants.cullingViewSize = constants.viewSize;
		constants.cullingProjectionScale = Vector2::AnyInit(1.0f);
	}
	view_[ToViewIndex(view.kind)].Upload(constants);
}

void Engine::MeshBatchResources::UploadBatchData(const RenderDrawContext& drawContext,
	const RenderSceneBatch& batch, const std::span<const RenderItem* const>& items, const MeshGPUResource& gpuMesh) {

	// データクリア
	meshScratch_.clear();
	subMeshScratch_.clear();
	paletteScratch_.clear();
	skinnedRecords_.clear();
	skinnedVertexOffsetMap_.clear();
	skinnedInstanceCount_ = 0;
	skinningDispatched_ = false;
	usesFallbackTexture_ = false;
	// 描画アイテム数に応じて必要なバッファサイズを確保する
	if (meshScratch_.capacity() < items.size()) {
		meshScratch_.reserve(items.size());
	}

	// サブメッシュデータはインスタンスごとに必要なため、アイテム数×サブメッシュ数の容量を確保する
	size_t totalSubMeshCount = items.size() * gpuMesh.subMeshes.size();
	if (subMeshScratch_.capacity() < totalSubMeshCount) {

		subMeshScratch_.reserve(totalSubMeshCount);
	}
	// カメラ移動で可視数が増えた瞬間にGPUバッファを作り直さないよう、カリング前の最大数で先に確保する
	meshData_.EnsureCapacity(static_cast<uint32_t>((std::max)(items.size(), size_t(1))));
	subMeshData_.EnsureCapacity(static_cast<uint32_t>((std::max)(totalSubMeshCount, size_t(1))));

	GraphicsCore& graphicsCore = *drawContext.graphicsCore;

	// スキニング可能メッシュのときだけリソース生成する
	if (gpuMesh.isSkinned) {

		EnsureSkinningResources(graphicsCore);
	}

	// エラーテクスチャのSRVインデックスを取得する
	const GPUTextureResource* fallback = graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
	uint32_t fallbackSRVIndex = (fallback && fallback->srvIndex != UINT32_MAX) ? fallback->srvIndex : 0;
	std::unordered_map<AssetID, uint32_t> baseColorSRVCache{};
	baseColorSRVCache.reserve(gpuMesh.subMeshes.size() + 1);

	// テクスチャアセットIDからSRVインデックスを取得するヘルパー
	// assetIDが無効なら UINT32_MAX を返す（シェーダー側で未使用として扱う）
	auto ResolveSRVIndex = [&](AssetID assetID, bool sRGB) -> uint32_t {

		if (!assetID) {
			return UINT32_MAX;
		}
		const GPUTextureResource* texture = RuntimeTextureResolver::Resolve(
			graphicsCore, drawContext.assetDatabase, assetID, sRGB);
		if (!texture || texture->srvIndex == UINT32_MAX) {
			return fallbackSRVIndex;
		}
		if (texture == fallback) {
			usesFallbackTexture_ = true;
		}
		return texture->srvIndex;
		};

	bool cullingEnabled = CanCullView(drawContext, gpuMesh);
	const ResolvedCameraView* cullingCamera = nullptr;
	if (cullingEnabled) {
		// カリング用カメラが取れない場合は全描画に倒す
		cullingCamera = drawContext.cullingView->FindCamera(RenderCameraDomain::Perspective);
		if (!cullingCamera) {
			cullingEnabled = false;
		}
	}

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
			instance.worldMatrix = RenderBillboard::ResolveWorldMatrix(*item, *drawContext.view);
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

		for (uint32_t subMeshIndex = 0; subMeshIndex < static_cast<uint32_t>(gpuMesh.subMeshes.size()); ++subMeshIndex) {

			// ベースカラーはsRGB、それ以外はLinear
			AssetID baseColorAsset = MeshDrawPathCommon::ResolveSubMeshBaseColorTextureAssetID(gpuMesh, renderer, subMeshIndex);
			uint32_t baseColorSRVIndex = fallbackSRVIndex;
			auto cachedTexture = baseColorSRVCache.find(baseColorAsset);
			if (cachedTexture != baseColorSRVCache.end()) {

				baseColorSRVIndex = cachedTexture->second;
			} else {

				if (baseColorAsset) {

					const GPUTextureResource* texture = RuntimeTextureResolver::Resolve(
						graphicsCore, drawContext.assetDatabase, baseColorAsset, true);
					baseColorSRVIndex = (texture && texture->srvIndex != UINT32_MAX) ? texture->srvIndex : fallbackSRVIndex;
					if (texture == fallback) {
						usesFallbackTexture_ = true;
					}
				}
				baseColorSRVCache.emplace(baseColorAsset, baseColorSRVIndex);
			}

			AssetID normalAsset = MeshDrawPathCommon::ResolveSubMeshNormalTextureAssetID(gpuMesh, renderer, subMeshIndex);
			AssetID metallicRoughnessAsset = MeshDrawPathCommon::ResolveSubMeshMetallicRoughnessTextureAssetID(gpuMesh, renderer, subMeshIndex);
			AssetID emissiveAsset = MeshDrawPathCommon::ResolveSubMeshEmissiveTextureAssetID(gpuMesh, renderer, subMeshIndex);
			AssetID occlusionAsset = MeshDrawPathCommon::ResolveSubMeshOcclusionTextureAssetID(gpuMesh, renderer, subMeshIndex);
			AssetID specularAsset = MeshDrawPathCommon::ResolveSubMeshSpecularTextureAssetID(gpuMesh, renderer, subMeshIndex);

			// サブメッシュデータの構築
			MeshSubMeshShaderData data{};
			data.baseColorTextureIndex = baseColorSRVIndex;
			data.normalTextureIndex = ResolveSRVIndex(normalAsset, false);
			data.metallicRoughnessTextureIndex = ResolveSRVIndex(metallicRoughnessAsset, false);
			data.emissiveTextureIndex = ResolveSRVIndex(emissiveAsset, true);
			data.occlusionTextureIndex = ResolveSRVIndex(occlusionAsset, false);
			data.specularTextureIndex = ResolveSRVIndex(specularAsset, false);

			data.importedBaseColor = gpuMesh.subMeshes[subMeshIndex].baseColor;
			if (renderer && subMeshIndex < renderer->subMeshes.size()) {

				const auto& authoring = renderer->subMeshes[subMeshIndex];
				data.color = authoring.color;
				data.emissiveColor = authoring.emissiveColor;
				data.metallic = authoring.metallic;
				data.roughness = authoring.roughness;
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
	const uint32_t prevVisibleCapacity = visibleMeshData_.GetCapacity();
	// 可視インスタンスRWバッファはカリング前のインスタンス数分だけ確保する
	visibleMeshData_.EnsureCapacity(static_cast<uint32_t>((std::max)(meshScratch_.size(), size_t(1))));
	if (prevVisibleCapacity != visibleMeshData_.GetCapacity()) {

		visibleMeshDataState_ = D3D12_RESOURCE_STATE_COMMON;
	}
	subMeshData_.Upload(subMeshScratch_);

	// 描画定数の転送
	MeshDrawConstants drawConstants{};
	drawConstants.meshletCount = gpuMesh.meshletCount;
	drawConstants.subMeshCount = static_cast<uint32_t>(gpuMesh.subMeshes.size());
	drawConstants.instanceCount = instanceCount_;
	// DrawPath/AS/CS側で参照するカリングON/OFFをまとめて渡す
	drawConstants.cullingEnabled = cullingEnabled ? 1u : 0u;
	drawConstants.packedMeshletVertexIndices = gpuMesh.usePackedMeshletVertexIndices ? 1u : 0u;
	drawConstants.contributionCullingEnabled = cullingEnabled && drawContext.runtimeFeatures.useContributionCulling ? 1u : 0u;
	drawConstants.normalConeCullingEnabled = cullingEnabled && drawContext.runtimeFeatures.useNormalConeCulling ? 1u : 0u;
	drawConstants.meshBoundsCenter = gpuMesh.boundsCenter;
	drawConstants.meshBoundsRadius = gpuMesh.boundsRadius;
	// 小さすぎる値はチラつきや誤カリングの原因になるため、控えめな閾値にしている
	drawConstants.contributionPixelThreshold = 0.5f;
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
		const uint32_t requiredSkinnedVertexCount = (std::max)(1u,
			static_cast<uint32_t>((std::max)(items.size(), size_t(1))) * gpuMesh.vertexCount);
		const uint32_t prevCapacity = skinning_->skinnedVertices.GetCapacity();
		skinning_->skinnedVertices.EnsureCapacity(requiredSkinnedVertexCount);
		const uint32_t prevPackedCapacity = skinning_->skinnedPackedVertices.GetCapacity();
		// 通常頂点と圧縮頂点で別リソースなので、容量変更も個別に見る
		skinning_->skinnedPackedVertices.EnsureCapacity(requiredSkinnedVertexCount);
		// スキニング頂点バッファの容量が変わった場合は、リソース状態をリセットする
		if (prevCapacity != skinning_->skinnedVertices.GetCapacity()) {

			skinning_->skinnedVertexState = D3D12_RESOURCE_STATE_COMMON;
		}
		if (prevPackedCapacity != skinning_->skinnedPackedVertices.GetCapacity()) {

			skinning_->skinnedPackedVertexState = D3D12_RESOURCE_STATE_COMMON;
		}
	}
}
