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
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/InvertedHullOutlineComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <cmath>
#include <unordered_map>
#include <variant>

//============================================================================
//	MeshBatchResources classMethods
//============================================================================
namespace {

	bool CanCullView(const Engine::RenderDrawContext& drawContext, const Engine::MeshGPUResource& gpuMesh) {

		// スキニングメッシュはCPU側での静的Boundsがずれやすいため、ここでは安全側で除外する
		return drawContext.view &&
			drawContext.cullingView &&
			drawContext.cullingView->valid &&
			!gpuMesh.isSkinned;
	}

	const Engine::MeshRendererComponent* ResolveRenderer(const Engine::RenderItem* item) {

		if (!item || !item->world) {
			return nullptr;
		}
		return item->world->TryGetComponent<Engine::MeshRendererComponent>(item->entity);
	}
	const Engine::SkinnedAnimationRuntimeData* ResolveSkinnedAnimationRuntime(
		const Engine::RenderItem* item) {

		if (!item || !item->world) {
			return nullptr;
		}
		return Engine::TryGetSkinnedAnimationRuntime(
			*item->world, item->entity);
	}
	const Engine::InvertedHullOutlineComponent* ResolveOutline(const Engine::RenderItem* item) {

		if (!item || !item->world) {
			return nullptr;
		}
		return item->world->TryGetComponent<Engine::InvertedHullOutlineComponent>(item->entity);
	}

	// Hull本体を描くパスかどうかでOutlineStencilWriteは元メッシュ形状なのでHullではない
	bool IsHullOutlinePass(Engine::MaterialPassKind passKind) {

		return passKind == Engine::MaterialPassKind::Outline ||
			passKind == Engine::MaterialPassKind::OutlineStencilTest;
	}

	uint64_t ComputeMaterialLayoutHash(
		const Engine::MaterialParameterLayout& layout) {

		uint64_t hash = 1469598103934665603ull;
		Engine::Algorithm::HashCombine(hash, layout.GetSizeInBytes());
		for (const Engine::ShaderConstantBufferVariable& variable :
			layout.GetVariables()) {

			Engine::Algorithm::HashCombine(hash,
				static_cast<uint64_t>(std::hash<std::string>{}(variable.name)));
			Engine::Algorithm::HashCombine(hash, variable.offset);
			Engine::Algorithm::HashCombine(hash, variable.size);
			Engine::Algorithm::HashCombine(hash,
				static_cast<uint64_t>(variable.valueClass));
			Engine::Algorithm::HashCombine(hash,
				static_cast<uint64_t>(variable.valueType));
		}
		return hash;
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
	// 可変stride構造化バッファを後から生成するため保持しておく
	device_ = device;
	srvDescriptor_ = srvDescriptor;

	// バッファ作成
	for (auto& viewBuffer : view_) {
		viewBuffer.Init(device);
	}
	BufferUploadService* uploadService =
		&graphicsCore.GetBufferUploadService();
	meshData_.Init(device, srvDescriptor, uploadService);
	// カリング後に残すインスタンスを書き込むRWバッファ
	visibleMeshData_.Init(device, srvDescriptor);
	subMeshData_.Init(device, srvDescriptor, uploadService);
	// 背面法アウトラインのインスタンス別GPUデータ
	outlineData_.Init(device, srvDescriptor, uploadService);
	DxUtils::CreateUavBufferResource(device, indexedIndirectArgs_,
		sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) * kMeshLODCount);
	indexedIndirectArgsState_ = D3D12_RESOURCE_STATE_COMMON;

	// 初期値を大きめにして、カメラ移動時の細かい再確保を減らす
	visibleMeshData_.EnsureCapacity(256);
	meshScratch_.reserve(256);
	subMeshScratch_.reserve(256);

	// 初期化完了
	initialized_ = true;
}

void Engine::MeshBatchResources::Finalize() {

	// OptionalSkinningResourcesは内部にSRV/UAV付きGPUバッファを持つため、終了時に明示resetする
	skinning_.reset();
	// パス別の可変strideマテリアルパラメータバッファを解放する
	for (auto& buffer : subMeshParamBuffers_) {

		if (buffer) {
			ReleaseSubMeshMaterialParamBuffer(*buffer);
			buffer.reset();
		}
	}
	activeSubMeshParamBuffer_ = nullptr;
	subMeshParamScratch_.clear();
	displacementMetricMaterial_ = nullptr;
	displacementMetricMaterialHash_ = 0;
	cachedMaxDisplacement_ = 0.0f;
	dynamicConstantAllocator_.Release();
	dynamicConstantFrameSerial_ = 0;
	viewUploadFrameSerials_ = { 0, 0 };
	drawGPUAddress_ = 0;
	screenSpaceOutlineMaskGPUAddress_ = 0;
	indirectArgsGPUAddress_ = 0;
	device_ = nullptr;
	srvDescriptor_ = nullptr;
	meshScratch_.clear();
	subMeshScratch_.clear();
	outlineScratch_.clear();
	paletteScratch_.clear();
	skinnedRecords_.clear();
	skinnedVertexOffsetMap_.clear();
	instanceCount_ = 0;
	skinnedInstanceCount_ = 0;
	skinningDispatched_ = false;
	skinningOutputValid_ = false;
	currentSkinningPoseHash_ = 0;
	dispatchedSkinningPoseHash_ = 0;
	skinningBufferGeneration_ = 0;
	usesFallbackTexture_ = false;
	indexedIndirectArgs_.Reset();
	indexedIndirectArgsState_ = D3D12_RESOURCE_STATE_COMMON;
	visibleMeshDataState_ = D3D12_RESOURCE_STATE_COMMON;
	initialized_ = false;
}

Engine::MeshBatchResources::SubMeshMaterialParamBuffer&
Engine::MeshBatchResources::GetSubMeshMaterialParamBuffer(
	MaterialPassKind passKind) {

	size_t index = static_cast<size_t>(passKind);
	if (kSubMeshMaterialPassBufferCount <= index) {
		index = static_cast<size_t>(MaterialPassKind::Invalid);
	}
	auto& buffer = subMeshParamBuffers_[index];
	if (!buffer) {
		buffer = std::make_unique<SubMeshMaterialParamBuffer>();
	}
	return *buffer;
}

void Engine::MeshBatchResources::ReleaseSubMeshMaterialParamBuffer(
	SubMeshMaterialParamBuffer& buffer) {

	if (srvDescriptor_) {
		for (uint32_t& index : buffer.srvIndices) {

			if (index != UINT32_MAX) {
				srvDescriptor_->Free(index);
				index = UINT32_MAX;
			}
		}
		for (const uint32_t index : buffer.retiredSrvIndices) {
			srvDescriptor_->Free(index);
		}
	}
	buffer.buffer.Release();
	buffer.handles = {};
	buffer.retiredSrvIndices.clear();
	buffer.packedScratch.clear();
	buffer.layoutHash = 0;
	buffer.materialHash = 0;
	buffer.material = nullptr;
	buffer.dataGeneration = 1;
	buffer.uploadedGenerations = { 0, 0, 0 };
	buffer.stride = 0;
	buffer.available = false;
	buffer.dirty = true;
}

void Engine::MeshBatchResources::BeginDynamicConstantsFrame() {

	const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
	if (dynamicConstantFrameSerial_ == frameSerial) {
		return;
	}
	dynamicConstantAllocator_.BeginFrame();
	dynamicConstantFrameSerial_ = frameSerial;
}

float Engine::MeshBatchResources::ResolveMaxDisplacement(
	const MaterialAsset* material) {

	if (!material) {
		return 0.0f;
	}

	const uint64_t materialHash = material->parameters.GetContentHash();
	if (displacementMetricMaterial_ == material &&
		displacementMetricMaterialHash_ == materialHash) {

		return cachedMaxDisplacement_;
	}

	const auto resolveValue = [&](const MaterialParameterSet& overrides,
		MaterialParameterID id) -> const MaterialParameterValue* {

		if (const MaterialParameterValue* value = overrides.Find(id)) {
			return value;
		}
		return material->parameters.Find(id);
	};
	const auto resolveFloat = [&](const MaterialParameterSet& overrides,
		MaterialParameterID id, float fallback) {

		const MaterialParameterValue* value = resolveValue(overrides, id);
		const float* result = value ? std::get_if<float>(&value->value) : nullptr;
		return result ? *result : fallback;
	};
	const auto resolveOne = [&](const MaterialParameterSet& overrides) {

		const MaterialParameterValue* texture = resolveValue(
			overrides, MaterialParameterIDs::DisplacementTexture);
		const AssetID* textureID = texture ?
			std::get_if<AssetID>(&texture->value) : nullptr;
		if (!textureID || !*textureID) {
			return 0.0f;
		}

		const float scale = resolveFloat(overrides,
			MaterialParameterIDs::DisplacementScale, 0.0f);
		const float midpoint = resolveFloat(overrides,
			MaterialParameterIDs::DisplacementMidpoint, 0.5f);
		const float heightRange = (std::max)(
			std::abs(midpoint), std::abs(1.0f - midpoint));
		return std::abs(scale) * heightRange;
	};

	cachedMaxDisplacement_ = 0.0f;
	if (subMeshParamScratch_.empty()) {
		static const MaterialParameterSet kEmptyOverrides{};
		cachedMaxDisplacement_ = resolveOne(kEmptyOverrides);
	} else {

		for (const MaterialParameterSet& overrides : subMeshParamScratch_) {
			cachedMaxDisplacement_ = (std::max)(
				cachedMaxDisplacement_, resolveOne(overrides));
		}
	}
	displacementMetricMaterial_ = material;
	displacementMetricMaterialHash_ = materialHash;
	return cachedMaxDisplacement_;
}

void Engine::MeshBatchResources::UpdateDrawConstants(const RenderDrawContext& drawContext,
	const MeshGPUResource& gpuMesh, uint32_t subMeshIndex,
	uint32_t subMeshGroupIndex, const MaterialAsset* material) {

	const bool hullOutline = IsHullOutlinePass(drawContext.passKind);
	bool canCull = CanCullView(drawContext, gpuMesh);
	if (canCull) {
		// カリング用カメラが取れない場合は全描画に倒す
		const ResolvedCameraView* cullingCamera = drawContext.cullingView->FindCamera(RenderCameraDomain::Perspective);
		if (!cullingCamera) {
			canCull = false;
		}
	}

	// ScreenPixelsでは近距離、投影、カメラ角度の影響を受ける
	// 誤カリングを避けるためHullのときだけ安全側でフラスタムカリングを無効にする
	if (hullOutline && outlineMetrics_.hasScreenPixelWidth) {
		canCull = false;
	}
	const bool frustumCullingEnabled =
		canCull &&
		drawContext.runtimeFeatures.useFrustumCulling;
	const bool contributionCullingEnabled =
		!hullOutline && canCull &&
		drawContext.runtimeFeatures.useContributionCulling;
	const bool normalConeCullingEnabled =
		!hullOutline && canCull &&
		drawContext.runtimeFeatures.useNormalConeCulling;
	const bool occlusionCullingEnabled =
		!hullOutline && canCull &&
		drawContext.passKind != MaterialPassKind::ZPrepass &&
		drawContext.passKind != MaterialPassKind::EditorPicking &&
		drawContext.runtimeFeatures.useOcclusionCulling &&
		drawContext.occlusionDepthPyramidReady;
	const bool cullingEnabled =
		frustumCullingEnabled ||
		contributionCullingEnabled ||
		normalConeCullingEnabled ||
		occlusionCullingEnabled;
	MeshDrawConstants drawConstants{};
	drawConstants.meshletCount = 0;
	drawConstants.subMeshCount = static_cast<uint32_t>(gpuMesh.subMeshes.size());
	drawConstants.instanceCount = instanceCount_;
	drawConstants.cullingEnabled = cullingEnabled ? 1u : 0u;
	drawConstants.packedMeshletVertexIndices = gpuMesh.usePackedMeshletVertexIndices ? 1u : 0u;
	drawConstants.frustumCullingEnabled =
		frustumCullingEnabled ? 1u : 0u;

	// 背面法では通常メッシュのnormal cone判定を流用できない
	// 線が小さくても見えるためcontribution cullingも無効化する
	drawConstants.contributionCullingEnabled =
		contributionCullingEnabled ? 1u : 0u;
	drawConstants.normalConeCullingEnabled =
		normalConeCullingEnabled ? 1u : 0u;
	drawConstants.occlusionCullingEnabled =
		occlusionCullingEnabled ? 1u : 0u;
	drawConstants.subMeshGroupIndex = subMeshGroupIndex;

	const float maxDisplacement = ResolveMaxDisplacement(material);
	drawConstants.meshBoundsCenter = gpuMesh.boundsCenter;
	drawConstants.meshBoundsRadius = gpuMesh.boundsRadius + maxDisplacement;
	drawConstants.maxDisplacement = maxDisplacement;
	// 小さすぎる値はチラつきや誤カリングの原因になるため、控えめな閾値にしている
	drawConstants.contributionPixelThreshold = 0.5f;
	drawConstants.lodPixelThresholds = Vector3(
		drawContext.runtimeFeatures.
			meshLOD0PixelThreshold,
		drawContext.runtimeFeatures.
			meshLOD1PixelThreshold,
		drawContext.runtimeFeatures.
			meshLOD2PixelThreshold);
	drawConstants.lodCount =
		drawContext.runtimeFeatures.useMeshLOD ?
		kMeshLODCount : 1u;

	drawConstants.invertedHullOutlinePass = hullOutline ? 1u : 0u;
	drawConstants.outlineMaxModelExpansion = hullOutline ? outlineMetrics_.maxModelExpansion : 0.0f;
	drawConstants.outlineMaxAbsCameraZOffset = hullOutline ? outlineMetrics_.maxAbsCameraZOffset : 0.0f;
	drawConstants.outlineHasScreenPixelWidth = (hullOutline && outlineMetrics_.hasScreenPixelWidth) ? 1u : 0u;
	const bool drawSingleSubMesh =
		subMeshIndex != kAllMeshSubMeshes &&
		subMeshIndex < gpuMesh.subMeshes.size();
	for (uint32_t lodIndex = 0; lodIndex < kMeshLODCount; ++lodIndex) {

		const MeshLODRange& lod = drawSingleSubMesh ?
			gpuMesh.subMeshes[subMeshIndex].lods[lodIndex] :
			gpuMesh.lods[lodIndex];
		drawConstants.lodIndexOffsets[lodIndex] = lod.indexOffset;
		drawConstants.lodIndexCounts[lodIndex] = lod.indexCount;
		drawConstants.lodMeshletOffsets[lodIndex] = lod.meshletOffset;
		drawConstants.lodMeshletCounts[lodIndex] = lod.meshletCount;
		drawConstants.meshletCount = (std::max)(
			drawConstants.meshletCount, lod.meshletCount);
	}

	BeginDynamicConstantsFrame();
	drawGPUAddress_ = dynamicConstantAllocator_.AllocateAndUpload(
		device_, drawConstants).gpuAddress;

	if (drawContext.passKind == MaterialPassKind::ScreenSpaceOutlineMask ||
		drawContext.passKind == MaterialPassKind::ScreenSpaceOutlineCoverageMask) {

		ScreenSpaceOutlineMaskConstants params{};
		params.styleID = drawContext.screenSpaceOutlineMaskStyleID;
		params.restrictSubMeshIndex = drawContext.screenSpaceOutlineMaskRestrictSubMeshIndex;
		params.alphaSource = drawContext.screenSpaceOutlineMaskAlphaSource;
		screenSpaceOutlineMaskGPUAddress_ =
			dynamicConstantAllocator_.AllocateAndUpload(
				device_, params).gpuAddress;
	}
}

void Engine::MeshBatchResources::UpdateIndexedIndirectArgsConstants(uint32_t indexCount) {

	// ComputeでDrawIndexedInstanced引数を組み立てるため、Index数だけCPUから渡す
	MeshIndirectArgsConstants constants{};
	constants.indexCount = indexCount;
	BeginDynamicConstantsFrame();
	indirectArgsGPUAddress_ =
		dynamicConstantAllocator_.AllocateAndUpload(
			device_, constants).gpuAddress;
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
	skinningOutputValid_ = false;
	++skinningBufferGeneration_;
}

bool Engine::MeshBatchResources::FindSkinnedVertexOffset(ECSWorld* world, Entity entity, uint32_t& outVertexOffset) const {

	MeshEntityLookupKey key{};
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

	const size_t viewIndex = ToViewIndex(view.kind);
	const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
	if (viewUploadFrameSerials_[viewIndex] == frameSerial) {
		return;
	}
	viewUploadFrameSerials_[viewIndex] = frameSerial;

	// 定数バッファにビュー行列を転送する
	MeshViewConstants constants{};
	if (const ResolvedCameraView* camera = view.FindCamera(RenderCameraDomain::Perspective)) {

		constants.viewProjection = camera->matrices.viewProjectionMatrix;
		constants.previousViewProjection = previousViewValid_[viewIndex] ?
			previousViewProjections_[viewIndex] : constants.viewProjection;
		previousViewProjections_[viewIndex] = constants.viewProjection;
		previousViewValid_[viewIndex] = true;
		constants.renderCameraPos = camera->cameraPos;
	}
	constants.frameSerial = static_cast<uint32_t>(frameSerial);
	constants.viewSize = Vector2(static_cast<float>((std::max)(view.width, 1u)),
		static_cast<float>((std::max)(view.height, 1u)));
	const ResolvedRenderView* cullView = cullingView ? cullingView : &view;
	if (const ResolvedCameraView* camera = cullView->FindCamera(RenderCameraDomain::Perspective)) {

		// SceneViewでは描画行列とカリング行列が別になるため、両方をGPUへ渡す
		constants.cullingViewProjection = camera->matrices.viewProjectionMatrix;
		constants.cullingView = camera->matrices.viewMatrix;
		constants.cullingCameraPos = camera->cameraPos;
		constants.cullingNearClip = camera->nearClip;
		constants.cullingCameraForward = camera->forward;
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
	view_[viewIndex].Upload(constants);
}

void Engine::MeshBatchResources::UploadBatchData(const RenderDrawContext& drawContext,
	const RenderSceneBatch& batch, const std::span<const RenderItem* const>& items, const MeshGPUResource& gpuMesh) {

	// staticキャッシュMISS時やSkinned/Billboardで毎フレーム走るバッチ構築のCPUコストを計測する
	FrameProfiler::ScopedSample profileSample(FrameProfiler::Category::MeshBatchUpload);

	// データクリア
	meshScratch_.clear();
	meshInstanceIndexMap_.clear();
	subMeshScratch_.clear();
	subMeshParamScratch_.clear();
	activeSubMeshParamBuffer_ = nullptr;
	for (auto& buffer : subMeshParamBuffers_) {
		if (buffer) {
			buffer->dirty = true;
		}
	}
	displacementMetricMaterial_ = nullptr;
	displacementMetricMaterialHash_ = 0;
	cachedMaxDisplacement_ = 0.0f;
	outlineScratch_.clear();
	paletteScratch_.clear();
	skinnedRecords_.clear();
	skinnedVertexOffsetMap_.clear();
	skinnedInstanceCount_ = 0;
	currentSkinningPoseHash_ = 1469598103934665603ull;
	usesFallbackTexture_ = false;
	// アウトラインの保守的メトリクスを初期化する
	outlineMetrics_ = OutlineBatchMetrics{};
	// インスタンスと同数のアウトラインデータを必ず作るため、先に容量を確保する
	outlineScratch_.reserve(items.size());
	// 描画アイテム数に応じて必要なバッファサイズを確保する
	if (meshScratch_.capacity() < items.size()) {
		meshScratch_.reserve(items.size());
	}

	// サブメッシュ分割バッチは対象スロットだけを転送し、多数スロット時の二乗的な転送を避ける
	uint32_t batchSubMeshIndex = kAllMeshSubMeshes;
	if (!items.empty()) {
		const MeshRenderPayload* payload =
			batch.GetPayload<MeshRenderPayload>(*items.front());
		if (payload && payload->subMeshIndex < gpuMesh.subMeshes.size()) {
			batchSubMeshIndex = payload->subMeshIndex;
		}
	}
	const uint32_t subMeshCountPerInstance =
		batchSubMeshIndex == kAllMeshSubMeshes ?
		static_cast<uint32_t>(gpuMesh.subMeshes.size()) : 1u;
	const size_t totalSubMeshCount =
		items.size() * static_cast<size_t>(subMeshCountPerInstance);
	if (subMeshScratch_.capacity() < totalSubMeshCount) {

		subMeshScratch_.reserve(totalSubMeshCount);
	}
	// カメラ移動で可視数が増えた瞬間にGPUバッファを作り直さないよう、カリング前の最大数で先に確保する

	GraphicsCore& graphicsCore = *drawContext.graphicsCore;

	// スキニング可能メッシュのときだけリソース生成する
	if (gpuMesh.isSkinned) {

		EnsureSkinningResources(graphicsCore);
	}

	// エラーテクスチャのSRVインデックスを取得する
	const GPUTextureResource* fallback = graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
	uint32_t fallbackSRVIndex = (fallback && fallback->srvIndex != UINT32_MAX) ? fallback->srvIndex : 0;

	// テクスチャアセットIDからSRVインデックスを取得するヘルパー
	// assetIDが無効ならUINT32_MAXを返しシェーダー側で未使用として扱う
	auto ResolveSRVIndex = [&](AssetID assetID, bool sRGB) -> uint32_t {

		if (!assetID) {
			return UINT32_MAX;
		}
		const GPUTextureResource* texture = RuntimeTextureResolver::Resolve(
			graphicsCore, drawContext.assetDatabase, assetID,
			sRGB ? TextureColorSpace::SRGB : TextureColorSpace::Linear);
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
		const std::span<const SubMeshMaterial> subMeshes =
			item->world ? GetMeshSubMeshes(*item->world, item->entity) :
			std::span<const SubMeshMaterial>{};
		const SkinnedAnimationRuntimeData* skinnedRuntime =
			ResolveSkinnedAnimationRuntime(item);
		std::vector<MeshSubMeshRenderState> renderGroups;
		std::vector<uint32_t> subMeshGroupIndices;
		if (renderer && !subMeshes.empty()) {
			MeshDrawPathCommon::BuildSubMeshRenderGroups(
				*renderer, subMeshes,
				renderGroups, subMeshGroupIndices);
		}

		// MS/VS
		{
			const ResolvedRenderView* billboardView = drawContext.billboardView ? drawContext.billboardView : drawContext.view;
			MeshInstanceData instance{};
			instance.worldMatrix = RenderBillboard::ResolveWorldMatrix(*item, *billboardView);
			const bool billboard = instance.worldMatrix != item->worldMatrix;
			instance.previousWorldMatrix = billboard ?
				instance.worldMatrix : item->previousWorldMatrix;
			instance.motionFrameSerial = billboard ? 0u : item->motionFrameSerial;
			MeshNormalMatrixResult instanceNormal = BuildSafeMeshNormalMatrix(instance.worldMatrix);
			instance.normalMatrix = instanceNormal.matrix;
			instance.orientationSign = instanceNormal.orientationSign;
			// 色はサブメッシュ単位のreflection paramへ移したのでper-instance tintは白固定にする
			instance.color = Color4::White();
			instance.subMeshDataOffset = static_cast<uint32_t>(subMeshScratch_.size());
			instance.subMeshCount = subMeshCountPerInstance;

			// MeshRenderFlagsのうちピクセル側で参照するものをinstance.flagsへ写す
			MeshRenderFlags renderFlags = renderer ? renderer->renderFlags : MeshRenderFlags::Default;
			SetMeshRenderFlag(renderFlags,
				MeshRenderFlags::ReceiveShadow,
				item->receiveShadows);
			if (HasMeshRenderFlag(renderFlags, MeshRenderFlags::Lighting)) {
				instance.flags |= kMeshInstanceFlagLighting;
			}
			if (HasMeshRenderFlag(renderFlags, MeshRenderFlags::ReceiveShadow)) {
				instance.flags |= kMeshInstanceFlagReceiveShadow;
			}
			if (HasMeshRenderFlag(renderFlags, MeshRenderFlags::ReceiveIBL)) {
				instance.flags |= kMeshInstanceFlagReceiveIBL;
			}
			if (HasMeshRenderFlag(renderFlags, MeshRenderFlags::ReceiveReflection)) {
				instance.flags |= kMeshInstanceFlagReceiveReflection;
			}
			if (renderer) {
				instance.flags |=
					(renderer->renderingLayerMask &
						kRenderingLayerMaskBits) << 8;
			}

			// スキニングする場合の設定
			if (gpuMesh.isSkinned && skinning_ && skinnedRuntime &&
				skinnedRuntime->initialized &&
				skinnedRuntime->palette.size() == gpuMesh.boneCount) {

				instance.flags |= kMeshInstanceFlagSkinned;
				instance.skinnedVertexOffset = skinnedInstanceCount_ * gpuMesh.vertexCount;

				// スキニングパレットデータを追加
				paletteScratch_.insert(paletteScratch_.end(),
					skinnedRuntime->palette.begin(), skinnedRuntime->palette.end());
				Algorithm::HashCombine(currentSkinningPoseHash_,
					static_cast<uint64_t>(item->entity.index));
				Algorithm::HashCombine(currentSkinningPoseHash_,
					static_cast<uint64_t>(item->entity.generation));
				Algorithm::HashCombine(currentSkinningPoseHash_,
					skinnedRuntime->poseGeneration);

				// スキニングするインスタンスのレコードを追加
				skinnedRecords_.push_back({ item->world,item->entity,instance.skinnedVertexOffset });
				MeshEntityLookupKey key{};
				key.world = item->world;
				key.entity = item->entity;
				skinnedVertexOffsetMap_[key] = instance.skinnedVertexOffset;

				// スキニングインスタンス数を加算
				++skinnedInstanceCount_;
			}

			// アウトラインGPUデータをインスタンスごとに必ず1件作る
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
				outlineMetrics_.maxAbsCameraZOffset = (std::max)(outlineMetrics_.maxAbsCameraZOffset, std::abs(outlineGPU.cameraZOffset));
			}

			instance.outlineDataIndex = static_cast<uint32_t>(outlineScratch_.size());
			instance.entityIndex = item->entity.index;
			instance.entityGeneration = item->entity.generation;
			outlineScratch_.emplace_back(outlineGPU);

			MeshEntityLookupKey instanceKey{};
			instanceKey.world = item->world;
			instanceKey.entity = item->entity;
			meshInstanceIndexMap_.emplace(
				instanceKey,
				static_cast<uint32_t>(meshScratch_.size()));
			meshScratch_.emplace_back(instance);
		}

		for (uint32_t localSubMeshIndex = 0;
			localSubMeshIndex < subMeshCountPerInstance;
			++localSubMeshIndex) {

			const uint32_t subMeshIndex =
				batchSubMeshIndex == kAllMeshSubMeshes ?
				localSubMeshIndex : batchSubMeshIndex;

			// 色やテクスチャはreflection paramへ移したのでgSubMeshesには幾何情報のみ詰める
			MeshSubMeshShaderData data{};
			data.importedBaseColor = gpuMesh.subMeshes[subMeshIndex].baseColor;
			if (subMeshIndex < subMeshes.size()) {

				const auto& authoring = subMeshes[subMeshIndex];
				// 保存値からGPU転送値を構築し、Componentへ実行時行列を書き戻さない
				data.uvMatrix = MeshSubMeshRuntime::BuildUVMatrix(authoring);
				data.localMatrix = MeshSubMeshRuntime::BuildRenderLocalMatrix(authoring);
				// localMatrixからも法線変換行列を構築し最終的にinstance.normalMatrixと合成される
				const MeshNormalMatrixResult localNormal = BuildSafeMeshNormalMatrix(data.localMatrix);
				data.localNormalMatrix = localNormal.matrix;
				data.localOrientationSign = localNormal.orientationSign;
				// Position Scaling膨張の基準で原点基準にならないようサブメッシュのピボットを渡す
				data.sourcePivot = authoring.sourcePivot;
				if (subMeshIndex < subMeshGroupIndices.size()) {
					data.renderGroupIndex =
						subMeshGroupIndices[subMeshIndex];
				}
				// reflection paramの上書きをインスタンス×サブメッシュ単位で集める
				MaterialParameterSet materialParams = authoring.materialInstance;
				MaterialParameterValue alphaClip{};
				alphaClip.value = item->surfaceMode == MaterialSurfaceMode::Masked ?
					authoring.alphaCutoff : 0.0f;
				materialParams.Set(
					MaterialParameterIDs::AlphaClip,
					MaterialParameterNames::AlphaClip,
					MaterialParameterSemantic::AlphaClip,
					alphaClip);
				subMeshParamScratch_.emplace_back(std::move(materialParams));
			} else {

				// rendererが無いときも要素数をgSubMeshesと揃える
				subMeshParamScratch_.emplace_back();
			}
			subMeshScratch_.emplace_back(data);
		}
	}

	// インスタンス数を設定
	instanceCount_ = static_cast<uint32_t>(meshScratch_.size());
	Algorithm::HashCombine(currentSkinningPoseHash_, skinnedInstanceCount_);

	// 静的データはDEFAULT heapへまとめて転送する
	meshData_.MarkFullUpdate(
		static_cast<uint32_t>(meshScratch_.size()));
	const uint32_t prevVisibleCapacity = visibleMeshData_.GetCapacity();
	// 可視インスタンスRWバッファはカリング前のインスタンス数分だけ確保する
	const size_t visibleCapacity =
		(std::max)(meshScratch_.size(), size_t(1)) * kMeshLODCount;
	visibleMeshData_.EnsureCapacity(static_cast<uint32_t>(visibleCapacity));
	if (prevVisibleCapacity != visibleMeshData_.GetCapacity()) {

		visibleMeshDataState_ = D3D12_RESOURCE_STATE_COMMON;
	}
	subMeshData_.MarkFullUpdate(
		static_cast<uint32_t>(subMeshScratch_.size()));
	// アウトラインGPUデータの転送でMeshDrawConstantsはUpdateDrawConstantsで毎描画更新する
	outlineData_.MarkFullUpdate(
		static_cast<uint32_t>(outlineScratch_.size()));

	if (skinning_) {

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
			skinningOutputValid_ = false;
			++skinningBufferGeneration_;
		}
		if (prevPackedCapacity != skinning_->skinnedPackedVertices.GetCapacity()) {

			skinning_->skinnedPackedVertexState = D3D12_RESOURCE_STATE_COMMON;
			skinningOutputValid_ = false;
			++skinningBufferGeneration_;
		}

		skinningDispatched_ = skinningOutputValid_ &&
			currentSkinningPoseHash_ == dispatchedSkinningPoseHash_;
		if (!skinningDispatched_ && 0 < skinnedInstanceCount_) {

			// ポーズ変更時だけパレットとDispatch定数を更新する
			skinning_->skinningPalette.Upload(paletteScratch_);

			MeshSkinningDispatchConstants skinningConstants{};
			skinningConstants.vertexCount = gpuMesh.vertexCount;
			skinningConstants.boneCount = gpuMesh.boneCount;
			skinningConstants.skinnedInstanceCount = skinnedInstanceCount_;
			skinning_->skinningConstants.Upload(skinningConstants);
		}
	}
	UploadCachedBatchData();
}

bool Engine::MeshBatchResources::RefreshInstanceTransforms(
	std::span<const RenderTransformChange> changes) {

	for (const RenderTransformChange& change : changes) {

		MeshEntityLookupKey key{};
		key.world = change.world;
		key.entity = change.entity;
		const auto [begin, end] =
			meshInstanceIndexMap_.equal_range(key);
		for (auto it = begin; it != end; ++it) {
			MeshInstanceData& instance =
				meshScratch_[it->second];
			if (instance.worldMatrix ==
				change.worldMatrix) {
				continue;
			}

			instance.previousWorldMatrix = change.previousWorldMatrix;
			instance.worldMatrix = change.worldMatrix;
			instance.motionFrameSerial = change.motionFrameSerial;
			const MeshNormalMatrixResult normal =
				BuildSafeMeshNormalMatrix(
					instance.worldMatrix);
			instance.normalMatrix = normal.matrix;
			instance.orientationSign =
				normal.orientationSign;
			meshData_.MarkDirtyRange(
				it->second, 1);
		}
	}
	return true;
}

void Engine::MeshBatchResources::UploadCachedBatchData() {

	// 各Frame Contextへ未反映の範囲だけ転送する
	meshData_.UploadCurrentFrame(meshScratch_);
	subMeshData_.UploadCurrentFrame(subMeshScratch_);
	outlineData_.UploadCurrentFrame(outlineScratch_);
}

void Engine::MeshBatchResources::UploadSubMeshMaterialParams(const MaterialAsset* material,
	const MaterialParameterLayout& layout, const RenderDrawContext& drawContext) {

	// シェーダーがMaterialParameters構造化バッファを宣言していないバッチはここで早期に無効化する
	activeSubMeshParamBuffer_ = nullptr;
	if (!layout.IsValid() || subMeshParamScratch_.empty() || !device_ || !srvDescriptor_) {
		return;
	}
	SubMeshMaterialParamBuffer& buffer =
		GetSubMeshMaterialParamBuffer(drawContext.passKind);
	buffer.available = false;

	bool usedFallbackTexture = false;

	// テクスチャSemanticからsRGB可否を決めてbindless indexへ解決する
	// 未指定はkNoTextureを返しシェーダー側でテクスチャなしの分岐に乗せる
	auto resolveTexture = [&](MaterialParameterSemantic semantic,
		const AssetID& id) {

		const MaterialParameterBufferBuilder::TextureResolveResult result =
			BackendDrawCommon::ResolveMaterialTextureIndex(
				drawContext, semantic, id);
		usedFallbackTexture |= !result.cacheable;
		return result;
		};

	const MaterialParameterSet emptyMap{};
	const MaterialParameterSet& defaults =
		material ? material->parameters : emptyMap;

	// リフレクションから取得した構造体strideをそのまま使用する
	const uint32_t stride = layout.GetSizeInBytes();
	const uint32_t elementCount = static_cast<uint32_t>(subMeshParamScratch_.size());
	const uint64_t layoutHash = ComputeMaterialLayoutHash(layout);
	const uint64_t materialHash = material ?
		material->parameters.GetContentHash() : 0;
	const bool rebuildPacked = buffer.dirty ||
		buffer.layoutHash != layoutHash ||
		buffer.materialHash != materialHash ||
		buffer.material != material;
	if (rebuildPacked) {

		buffer.packedScratch.assign(
			static_cast<size_t>(stride) * elementCount, 0);
		for (uint32_t i = 0; i < elementCount; ++i) {

			const std::vector<uint8_t> element =
				MaterialParameterBufferBuilder::BuildElement(
					defaults, subMeshParamScratch_[i], layout, resolveTexture);
			const size_t copyBytes = (std::min)(
				static_cast<size_t>(stride), element.size());
			std::memcpy(
				buffer.packedScratch.data() +
					static_cast<size_t>(stride) * i,
				element.data(), copyBytes);
		}
		buffer.layoutHash = layoutHash;
		buffer.materialHash = materialHash;
		buffer.material = material;
		// 非同期読込中のErrorTextureを固定せず、実テクスチャへ切り替わるまで再解決する
		buffer.dirty = usedFallbackTexture;
		usesFallbackTexture_ |= usedFallbackTexture;
		++buffer.dataGeneration;
		if (buffer.dataGeneration == 0) {
			buffer.dataGeneration = 1;
			buffer.uploadedGenerations = { 0, 0, 0 };
		}
	}

	// 容量不足時は全フレーム分を拡張し、stride変更時はSRVだけを更新する
	const uint32_t requiredBytes =
		static_cast<uint32_t>(buffer.packedScratch.size());
	const std::string resourceName =
		"gMeshSubMeshMaterialParameters[" +
		std::to_string(static_cast<uint32_t>(drawContext.passKind)) + "]";
	const bool reallocated = buffer.buffer.EnsureCapacity(
		device_, requiredBytes, resourceName, 4096);
	if (reallocated || stride != buffer.stride ||
		buffer.srvIndices[0] == UINT32_MAX) {

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = (std::max)(
			static_cast<uint32_t>(buffer.buffer.GetCapacity()) / stride, 1u);
		srvDesc.Buffer.StructureByteStride = stride;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		for (uint32_t frameIndex = 0;
			frameIndex < kGraphicsFrameContextCount; ++frameIndex) {

			if (buffer.srvIndices[frameIndex] != UINT32_MAX) {
				buffer.retiredSrvIndices.emplace_back(
					buffer.srvIndices[frameIndex]);
				buffer.srvIndices[frameIndex] = UINT32_MAX;
			}
			srvDescriptor_->CreateSRV(
				buffer.srvIndices[frameIndex],
				buffer.buffer.GetResource(frameIndex), srvDesc);
			buffer.handles[frameIndex] =
				srvDescriptor_->GetGPUHandle(
					buffer.srvIndices[frameIndex]);
		}
		buffer.stride = stride;
		buffer.uploadedGenerations = { 0, 0, 0 };
	}

	const uint32_t frameIndex = GraphicsFrameState::GetCurrentIndex();
	if (!buffer.packedScratch.empty() &&
		buffer.uploadedGenerations[frameIndex] !=
			buffer.dataGeneration) {

		buffer.buffer.Write(
			buffer.packedScratch.data(),
			buffer.packedScratch.size());
		buffer.uploadedGenerations[frameIndex] =
			buffer.dataGeneration;
	}
	buffer.available = true;
	activeSubMeshParamBuffer_ = &buffer;
}
