#include "MeshBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/InvertedHullOutlineComponent.h>
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
	const Engine::InvertedHullOutlineComponent* ResolveOutline(const Engine::RenderItem* item) {

		if (!item || !item->world) {
			return nullptr;
		}
		return item->world->TryGetComponent<Engine::InvertedHullOutlineComponent>(item->entity);
	}

	// Hull本体を描くパスかどうか。OutlineStencilWriteは元メッシュ形状なのでHullではない
	bool IsHullOutlinePass(Engine::MaterialPassKind passKind) {

		return passKind == Engine::MaterialPassKind::Outline ||
			passKind == Engine::MaterialPassKind::OutlineStencilTest;
	}
}

Engine::MeshBatchResources::~MeshBatchResources() {

	Finalize();
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
	screenSpaceOutlineMask_.Init(device);
	subMeshData_.Init(device, srvDescriptor);
	// 背面法アウトラインのインスタンス別GPUデータ
	outlineData_.Init(device, srvDescriptor);
	DxUtils::CreateUavBufferResource(device, indexedIndirectArgs_, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));
	indexedIndirectArgsState_ = D3D12_RESOURCE_STATE_COMMON;

	// 初期値を大きめにして、カメラ移動時の細かい再確保を減らす
	meshData_.EnsureCapacity(256);
	visibleMeshData_.EnsureCapacity(256);
	subMeshData_.EnsureCapacity(256);
	// GetOutlineGPUAddressが常に有効なリソースを指すよう、初期容量を確保しておく
	outlineData_.EnsureCapacity(256);
	meshScratch_.reserve(256);
	subMeshScratch_.reserve(256);

	// 初期化完了
	initialized_ = true;
}

void Engine::MeshBatchResources::Finalize() {

	// OptionalSkinningResourcesは内部にSRV/UAV付きGPUバッファを持つため、終了時に明示resetする
	skinning_.reset();
	meshScratch_.clear();
	subMeshScratch_.clear();
	outlineScratch_.clear();
	paletteScratch_.clear();
	skinnedRecords_.clear();
	skinnedVertexOffsetMap_.clear();
	instanceCount_ = 0;
	skinnedInstanceCount_ = 0;
	skinningDispatched_ = false;
	usesFallbackTexture_ = false;
	indexedIndirectArgs_.Reset();
	indexedIndirectArgsState_ = D3D12_RESOURCE_STATE_COMMON;
	visibleMeshDataState_ = D3D12_RESOURCE_STATE_COMMON;
	initialized_ = false;
}

void Engine::MeshBatchResources::UpdateDrawConstants(const RenderDrawContext& drawContext,
	const MeshGPUResource& gpuMesh) {

	const bool hullOutline = IsHullOutlinePass(drawContext.passKind);
	bool cullingEnabled = CanCullView(drawContext, gpuMesh);
	if (cullingEnabled) {
		// カリング用カメラが取れない場合は全描画に倒す
		const ResolvedCameraView* cullingCamera = drawContext.cullingView->FindCamera(RenderCameraDomain::Perspective);
		if (!cullingCamera) {
			cullingEnabled = false;
		}
	}

	// ScreenPixelsでは近距離、投影、カメラ角度の影響を受ける
	// 誤カリングを避けるためHullのときだけ安全側でフラスタムカリングを無効にする
	if (hullOutline && outlineMetrics_.hasScreenPixelWidth) {
		cullingEnabled = false;
	}
	MeshDrawConstants drawConstants{};
	drawConstants.meshletCount = gpuMesh.meshletCount;
	drawConstants.subMeshCount = static_cast<uint32_t>(gpuMesh.subMeshes.size());
	drawConstants.instanceCount = instanceCount_;
	drawConstants.cullingEnabled = cullingEnabled ? 1u : 0u;
	drawConstants.packedMeshletVertexIndices = gpuMesh.usePackedMeshletVertexIndices ? 1u : 0u;

	// 背面法では通常メッシュのnormal cone判定を流用できない
	// 線が小さくても見えるためcontribution cullingも無効化する
	drawConstants.contributionCullingEnabled =
		(!hullOutline && cullingEnabled && drawContext.runtimeFeatures.useContributionCulling) ? 1u : 0u;
	drawConstants.normalConeCullingEnabled =
		(!hullOutline && cullingEnabled && drawContext.runtimeFeatures.useNormalConeCulling) ? 1u : 0u;

	drawConstants.meshBoundsCenter = gpuMesh.boundsCenter;
	drawConstants.meshBoundsRadius = gpuMesh.boundsRadius;
	// 小さすぎる値はチラつきや誤カリングの原因になるため、控えめな閾値にしている
	drawConstants.contributionPixelThreshold = 0.5f;

	drawConstants.invertedHullOutlinePass = hullOutline ? 1u : 0u;
	drawConstants.outlineMaxModelExpansion = hullOutline ? outlineMetrics_.maxModelExpansion : 0.0f;
	drawConstants.outlineMaxAbsCameraZOffset = hullOutline ? outlineMetrics_.maxAbsCameraZOffset : 0.0f;
	drawConstants.outlineHasScreenPixelWidth = (hullOutline && outlineMetrics_.hasScreenPixelWidth) ? 1u : 0u;

	draw_.Upload(drawConstants);

	if (drawContext.passKind == MaterialPassKind::ScreenSpaceOutlineMask ||
		drawContext.passKind == MaterialPassKind::ScreenSpaceOutlineCoverageMask) {

		ScreenSpaceOutlineMaskConstants params{};
		params.styleID = drawContext.screenSpaceOutlineMaskStyleID;
		params.restrictSubMeshIndex = drawContext.screenSpaceOutlineMaskRestrictSubMeshIndex;
		screenSpaceOutlineMask_.Upload(params);
	}
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
	outlineScratch_.clear();
	paletteScratch_.clear();
	skinnedRecords_.clear();
	skinnedVertexOffsetMap_.clear();
	skinnedInstanceCount_ = 0;
	skinningDispatched_ = false;
	usesFallbackTexture_ = false;
	// アウトラインの保守的メトリクスを初期化する
	outlineMetrics_ = OutlineBatchMetrics{};
	// インスタンスと同数のアウトラインデータを必ず作るため、先に容量を確保する
	outlineScratch_.reserve(items.size());
	outlineData_.EnsureCapacity(static_cast<uint32_t>((std::max)(items.size(), size_t(1))));
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
	// 元々ベースカラーテクスチャが設定されていないMesh用の白テクスチャ
	// 白を掛けてもベースカラー(importedBaseColor/color)がそのまま出るため、未設定時はこちらを使う
	const GPUTextureResource* whiteTexture = graphicsCore.GetBuiltinTextureLibrary().GetWhiteTexture();
	uint32_t whiteSRVIndex = (whiteTexture && whiteTexture->srvIndex != UINT32_MAX) ? whiteTexture->srvIndex : fallbackSRVIndex;
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

	for (const RenderItem* item : items) {

		const MeshRenderPayload* payload = batch.GetPayload<MeshRenderPayload>(*item);
		if (!payload) {
			continue;
		}

		const MeshRendererComponent* renderer = ResolveRenderer(item);
		const SkinnedAnimationComponent* skinnedAnim = ResolveSkinnedAnimation(item);

		// MS/VS
		{
			const ResolvedRenderView* billboardView = drawContext.billboardView ? drawContext.billboardView : drawContext.view;
			MeshInstanceData instance{};
			instance.worldMatrix = RenderBillboard::ResolveWorldMatrix(*item, *billboardView);
			MeshNormalMatrixResult instanceNormal = BuildSafeMeshNormalMatrix(instance.worldMatrix);
			instance.normalMatrix = instanceNormal.matrix;
			instance.orientationSign = instanceNormal.orientationSign;
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

			// アウトラインGPUデータをインスタンスごとに必ず1件作る
			// コンポーネントが無い通常メッシュにもゼロ初期値を入れて対応を崩さない
			MeshOutlineGPUData outlineGPU{};
			if (const InvertedHullOutlineComponent* outline = ResolveOutline(item)) {

				outlineGPU.color = outline->color;
				outlineGPU.width = (std::max)(0.0f, outline->width);
				outlineGPU.cameraZOffset = outline->cameraZOffset;
				outlineGPU.expansionMode = static_cast<uint32_t>(outline->expansionMode);
				outlineGPU.widthMode = static_cast<uint32_t>(outline->widthMode);

				// Baked Normal / Outline SamplerはLinearとして解決する
				if (outline->useBakedNormal && outline->bakedNormalTexture) {
					outlineGPU.flags |= kMeshOutlineFlagUseBakedNormal;
					outlineGPU.bakedNormalTextureIndex = ResolveSRVIndex(outline->bakedNormalTexture, false);
				}
				if (outline->useOutlineSampler && outline->outlineSamplerTexture) {
					outlineGPU.flags |= kMeshOutlineFlagUseOutlineSampler;
					outlineGPU.outlineSamplerTextureIndex = ResolveSRVIndex(outline->outlineSamplerTexture, false);
				}

				// AS/instance-culling CS用の安全側メトリクスを更新する
				if (outlineGPU.widthMode == static_cast<uint32_t>(OutlineWidthMode::ScreenPixels)) {
					outlineMetrics_.hasScreenPixelWidth = true;
				} else {
					outlineMetrics_.maxModelExpansion = (std::max)(outlineMetrics_.maxModelExpansion, outlineGPU.width);
				}
				outlineMetrics_.maxAbsCameraZOffset = (std::max)(
					outlineMetrics_.maxAbsCameraZOffset, std::abs(outlineGPU.cameraZOffset));
			}

			instance.outlineDataIndex = static_cast<uint32_t>(outlineScratch_.size());
			outlineScratch_.emplace_back(outlineGPU);

			meshScratch_.emplace_back(instance);
		}

		for (uint32_t subMeshIndex = 0; subMeshIndex < static_cast<uint32_t>(gpuMesh.subMeshes.size()); ++subMeshIndex) {

			// ベースカラーはsRGB、それ以外はLinear
			AssetID baseColorAsset = MeshDrawPathCommon::ResolveSubMeshBaseColorTextureAssetID(gpuMesh, renderer, subMeshIndex);
			uint32_t baseColorSRVIndex;
			if (baseColorAsset) {

				// 解決対象は重複解決を避けるためAssetID単位でキャッシュする
				// 割り当て済みだが見つからない(解決失敗)場合はエラーテクスチャにフォールバックする
				auto cachedTexture = baseColorSRVCache.find(baseColorAsset);
				if (cachedTexture != baseColorSRVCache.end()) {

					baseColorSRVIndex = cachedTexture->second;
				} else {

					const GPUTextureResource* texture = RuntimeTextureResolver::Resolve(
						graphicsCore, drawContext.assetDatabase, baseColorAsset, true);
					baseColorSRVIndex = (texture && texture->srvIndex != UINT32_MAX) ? texture->srvIndex : fallbackSRVIndex;
					if (texture == fallback) {
						usesFallbackTexture_ = true;
					}
					baseColorSRVCache.emplace(baseColorAsset, baseColorSRVIndex);
				}
			} else {

				// 解決後AssetIDが空。元々割り当てがある(マテリアルで宣言済みだが見つからない)ならエラー、
				// 未割り当て(テクスチャなし)なら白にフォールバックする
				const bool assigned = MeshDrawPathCommon::WasSubMeshBaseColorTextureAssigned(gpuMesh, renderer, subMeshIndex);
				baseColorSRVIndex = assigned ? fallbackSRVIndex : whiteSRVIndex;
				if (assigned) {
					usesFallbackTexture_ = true;
				}
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
				// localMatrixからも法線変換行列を構築する。最終的にinstance.normalMatrixと合成される
				const MeshNormalMatrixResult localNormal = BuildSafeMeshNormalMatrix(data.localMatrix);
				data.localNormalMatrix = localNormal.matrix;
				data.localOrientationSign = localNormal.orientationSign;
				// Position Scaling膨張の基準。原点基準にならないようサブメッシュのピボットを渡す
				data.sourcePivot = authoring.sourcePivot;
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
	// アウトラインGPUデータの転送。MeshDrawConstantsはUpdateDrawConstantsで毎描画更新する
	outlineData_.Upload(outlineScratch_);

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
