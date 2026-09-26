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
	viewResources_.Init(graphicsCore.GetDXObject().GetResourceRetirement(), device);
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

	// MeshSkinningBufferSetは内部にSRV/UAV付きGPUバッファを持つため、終了時に明示resetする
	skinning_.reset();
	// パス別の可変strideマテリアルパラメータバッファを解放する
	materialBuffers_.Release(srvDescriptor_);
	viewResources_.Release();
	subMeshParamScratch_.clear();
	subMeshParamGenerations_.clear();
	cachedInstances_.clear();
	cachedMesh_ = {};
	cachedMeshGeneration_ = 0;
	parameterGeneration_ = 1;
	displacementMetricMaterial_ = nullptr;
	displacementMetricMaterialHash_ = 0;
	cachedMaxDisplacement_ = 0.0f;
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

	viewResources_.UpdateDrawConstants(drawContext, gpuMesh, subMeshIndex, subMeshGroupIndex,
		device_, instanceCount_, outlineMetrics_, ResolveMaxDisplacement(material));
}

void Engine::MeshBatchResources::UpdateIndexedIndirectArgsConstants(uint32_t indexCount) {

	viewResources_.UpdateIndexedIndirectArgsConstants(indexCount, device_);
}

void Engine::MeshBatchResources::EnsureSkinningResources(GraphicsCore& graphicsCore) {

	// すでにスキニング用のリソースがある場合は何もしない
	if (skinning_) {
		return;
	}

	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	SRVDescriptor* srvDescriptor = &graphicsCore.GetSRVDescriptor();

	// スキニング用のリソースを作成する
	skinning_ = std::make_unique<MeshSkinningBufferSet>();
	skinning_->Init(device, srvDescriptor);
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

	viewResources_.UpdateView(view, cullingView);
}

void Engine::MeshBatchResources::UploadBatchData(const RenderDrawContext& drawContext,
	const RenderSceneBatch& batch, const std::span<const RenderItem* const>& items, const MeshGPUResource& gpuMesh) {

	BuildBatchData(drawContext, batch, items, gpuMesh);
	UploadCachedBatchData();
}

void Engine::MeshBatchResources::UploadCachedBatchData() {

	FrameProfiler::ScopedSample total(FrameProfiler::Category::MeshBatchUpload);

	FrameProfiler::ScopedSample transfer(FrameProfiler::Category::MeshBufferTransfer);
	// 各Frame Contextへ未反映の範囲だけ転送する
	const uint64_t bytes = meshData_.UploadCurrentFrame(meshScratch_) +
		subMeshData_.UploadCurrentFrame(subMeshScratch_) + outlineData_.UploadCurrentFrame(outlineScratch_);
	FrameProfiler::GetInstance().AddMeshTransferBytes(bytes);
}

void Engine::MeshBatchResources::UploadSubMeshMaterialParams(const MaterialAsset* material,
	const MaterialParameterLayout& layout, const RenderDrawContext& drawContext) {

	materialBuffers_.UploadSubMeshMaterialParams(material, layout, drawContext, device_, srvDescriptor_,
		subMeshParamScratch_, subMeshParamGenerations_, parameterGeneration_, usesFallbackTexture_);
}

//============================================================================
//	MeshBatchResources classMethods
//============================================================================

namespace Engine {

	bool MeshEntityLookupKey::operator==(const MeshEntityLookupKey& rhs) const noexcept {

		return world == rhs.world && entity.index == rhs.entity.index &&
			entity.generation == rhs.entity.generation;
	}

	size_t MeshEntityLookupKeyHash::operator()(const MeshEntityLookupKey& key) const noexcept {

		size_t h = std::hash<void*>{}(key.world);
		h ^= (std::hash<uint32_t>{}(key.entity.index) << 1);
		h ^= (std::hash<uint32_t>{}(key.entity.generation) << 2);
		return h;
	}

	void MeshBatchResources::MarkSkinningDispatched() {

		skinningDispatched_ = true;
		skinningOutputValid_ = true;
		dispatchedSkinningPoseHash_ = currentSkinningPoseHash_;
	}

	D3D12_GPU_VIRTUAL_ADDRESS MeshBatchResources::GetSubMeshMaterialParamGPUAddress() const {

		return materialBuffers_.GetGPUAddress();
	}

	D3D12_GPU_DESCRIPTOR_HANDLE MeshBatchResources::GetSubMeshMaterialParamGPUHandle() const {

		return materialBuffers_.GetGPUHandle();
	}
}
