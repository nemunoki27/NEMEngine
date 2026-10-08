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

	// 描画対象のMesh設定を取得
	const Engine::MeshRendererComponent* ResolveRenderer(const Engine::RenderItem* item) {

		if (!item || !item->world) {
			return nullptr;
		}
		return item->world->TryGetComponent<Engine::MeshRendererComponent>(item->entity);
	}
	// Skinningの実行状態を読み取り専用で取得
	const Engine::SkinnedAnimationRuntimeData* ResolveSkinnedAnimationRuntime(const Engine::RenderItem* item) {

		if (!item || !item->world) {
			return nullptr;
		}
		const Engine::ECSWorld& world = *item->world;
		return Engine::TryGetSkinnedAnimationRuntime(world, item->entity);
	}
	// 背面Outlineの設定を取得
	const Engine::InvertedHullOutlineComponent* ResolveOutline(const Engine::RenderItem* item) {

		if (!item || !item->world) {
			return nullptr;
		}
		return item->world->TryGetComponent<Engine::InvertedHullOutlineComponent>(item->entity);
	}

}

void Engine::MeshBatchResources::BuildBatchData(const RenderDrawContext& drawContext,
	const RenderSceneBatch& batch, std::span<const RenderItem* const> items, const MeshGPUResource& gpuMesh) {

	// バッチ再構築と転送を別の区間で計測する
	FrameProfiler::ScopedSample profileSample(FrameProfiler::Category::MeshBatchUpload);
	FrameProfiler::ScopedSample buildSample(FrameProfiler::Category::MeshBatchBuild);
	FrameProfiler::GetInstance().AddMeshUpdate(1, 0, 0, 0, items.size());

	// データクリア
	meshScratch_.clear();
	meshInstanceIndexMap_.clear();
	subMeshScratch_.clear();
	subMeshParamScratch_.clear();
	materialBuffers_.ResetActive();
	materialBuffers_.Invalidate();
	displacementMetricMaterial_ = nullptr;
	displacementMetricMaterialHash_ = 0;
	cachedMaxDisplacement_ = 0.0f;
	outlineScratch_.clear();
	paletteScratch_.clear();
	skinnedVertexOffsetMap_.clear();
	skinnedInstanceCount_ = 0;
	skinningDrawEnabled_ = true;
	currentSkinningPoseHash_ = 1469598103934665603ull;
	Algorithm::HashCombine(currentSkinningPoseHash_, gpuMesh.reloadGeneration);
	usesFallbackTexture_ = false;
	// アウトラインの保守的メトリクスを初期化する
	outlineMetrics_ = OutlineBatchMetrics{};
	// Instance数に合わせてOutlineの容量を確保
	outlineScratch_.reserve(items.size());
	// 描画アイテム数に応じて必要なバッファサイズを確保する
	if (meshScratch_.capacity() < items.size()) {
		meshScratch_.reserve(items.size());
	}

	// 分割バッチは対象SubMeshだけ転送
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
	GraphicsCore& graphicsCore = *drawContext.graphicsCore;

	// スキニング可能メッシュのときだけリソース生成する
	if (gpuMesh.isSkinned) {

		EnsureSkinningResources(graphicsCore);
	}

	// エラーテクスチャのSRVインデックスを取得する
	const GPUTextureResource* fallback = graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
	uint32_t fallbackSRVIndex = (fallback && fallback->srvIndex != UINT32_MAX) ? fallback->srvIndex : 0;

	// 未指定Textureは未使用の番号を返す
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
			MeshDrawPathCommon::BuildSubMeshRenderGroups(*renderer, subMeshes, renderGroups, subMeshGroupIndices);
		}

		// 描画経路で共通のインスタンスを構築
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
			// 色の上書きはサブメッシュ側へ渡す
			instance.color = Color4::White();
			instance.subMeshDataOffset = static_cast<uint32_t>(subMeshScratch_.size());
			instance.subMeshCount = subMeshCountPerInstance;

			// PixelShaderへ渡す描画フラグを設定
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

				// スキニング頂点の位置を検索表へ登録
				MeshEntityLookupKey key{};
				key.world = item->world;
				key.entity = item->entity;
				skinnedVertexOffsetMap_[key] = instance.skinnedVertexOffset;

				// スキニングインスタンス数を加算
				++skinnedInstanceCount_;
			}

			// InstanceごとにOutlineの転送値を生成
			MeshOutlineGPUData outlineGPU{};
			if (const InvertedHullOutlineComponent* outline = ResolveOutline(item)) {

				outlineGPU.color = outline->color;
				outlineGPU.width = (std::max)(0.0f, outline->width);
				outlineGPU.cameraZOffset = outline->cameraZOffset;
				outlineGPU.expansionMode = static_cast<uint32_t>(outline->expansionMode);
				outlineGPU.widthMode = static_cast<uint32_t>(outline->widthMode);
				outlineGPU.alphaThreshold = std::clamp(
					outline->alphaThreshold, 0.0f, 1.0f);
				if (outline->respectMaterialSurface) {
					outlineGPU.flags |= kMeshOutlineFlagRespectMaterialSurface;
				}

				// 輪郭の法線と幅のTextureをLinearで取得
				if (outline->useBakedNormal && outline->bakedNormalTexture) {
					outlineGPU.flags |= kMeshOutlineFlagUseBakedNormal;
					outlineGPU.bakedNormalTextureIndex = ResolveSRVIndex(outline->bakedNormalTexture, false);
				}
				if (outline->useOutlineSampler && outline->outlineSamplerTexture) {
					outlineGPU.flags |= kMeshOutlineFlagUseOutlineSampler;
					outlineGPU.outlineSamplerTextureIndex = ResolveSRVIndex(outline->outlineSamplerTexture, false);
				}

				// 輪郭の膨張範囲をカリングへ反映
				if (outlineGPU.widthMode == static_cast<uint32_t>(OutlineWidthMode::ScreenPixels)) {
					outlineMetrics_.hasScreenPixelWidth = true;
				} else {
					outlineMetrics_.maxModelExpansion = (std::max)(outlineMetrics_.maxModelExpansion, outlineGPU.width);
				}
				outlineMetrics_.maxAbsCameraZOffset = (std::max)(
					outlineMetrics_.maxAbsCameraZOffset, std::abs(outlineGPU.cameraZOffset));
			}

			instance.outlineDataIndex = static_cast<uint32_t>(outlineScratch_.size());
			instance.entityIndex = item->entity.index;
			instance.entityGeneration = item->entity.generation;
			outlineScratch_.emplace_back(outlineGPU);

			MeshEntityLookupKey instanceKey{};
			instanceKey.world = item->world;
			instanceKey.entity = item->entity;
			meshInstanceIndexMap_.emplace(instanceKey, static_cast<uint32_t>(meshScratch_.size()));
			meshScratch_.emplace_back(instance);
		}

		for (uint32_t localSubMeshIndex = 0;
			localSubMeshIndex < subMeshCountPerInstance;
			++localSubMeshIndex) {

			const uint32_t subMeshIndex =
				batchSubMeshIndex == kAllMeshSubMeshes ?
				localSubMeshIndex : batchSubMeshIndex;

			// サブメッシュの形状データを構築
			MeshSubMeshShaderData data{};
			data.importedBaseColor = gpuMesh.subMeshes[subMeshIndex].baseColor;
			if (subMeshIndex < subMeshes.size()) {

				const auto& authoring = subMeshes[subMeshIndex];
				// 保存値から描画用の行列を構築
				data.uvMatrix = MeshSubMeshRuntime::BuildUVMatrix(authoring);
				data.localMatrix = MeshSubMeshRuntime::BuildRenderLocalMatrix(authoring);
				// サブメッシュの法線変換を構築
				const MeshNormalMatrixResult localNormal = BuildSafeMeshNormalMatrix(data.localMatrix);
				data.localNormalMatrix = localNormal.matrix;
				data.localOrientationSign = localNormal.orientationSign;
				// 輪郭を膨張する基準点を設定
				data.sourcePivot = authoring.sourcePivot;
				if (subMeshIndex < subMeshGroupIndices.size()) {
					data.renderGroupIndex =
						subMeshGroupIndices[subMeshIndex];
				}
				// インスタンスとサブメッシュごとにMaterial値を収集
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

				// 設定がなくてもSubMeshの要素数を揃える
				subMeshParamScratch_.emplace_back();
			}
			subMeshScratch_.emplace_back(data);
		}
	}

	// インスタンス数を設定
	instanceCount_ = static_cast<uint32_t>(meshScratch_.size());
	Algorithm::HashCombine(currentSkinningPoseHash_, skinnedInstanceCount_);

	// 静的データをまとめて転送
	meshData_.MarkFullUpdate(static_cast<uint32_t>(meshScratch_.size()));
	const uint32_t prevVisibleCapacity = visibleMeshData_.GetCapacity();
	// カリング前のInstance数に合わせて容量を確保
	const size_t visibleCapacity =
		(std::max)(meshScratch_.size(), size_t(1)) * kMeshLODCount;
	visibleMeshData_.EnsureCapacity(static_cast<uint32_t>(visibleCapacity));
	if (prevVisibleCapacity != visibleMeshData_.GetCapacity()) {

		visibleMeshDataState_ = D3D12_RESOURCE_STATE_COMMON;
	}
	subMeshData_.MarkFullUpdate(static_cast<uint32_t>(subMeshScratch_.size()));
	// 輪郭のインスタンスデータを転送対象へ追加
	outlineData_.MarkFullUpdate(static_cast<uint32_t>(outlineScratch_.size()));

	if (skinning_) {

		// スキニング頂点バッファの容量を確保
		const uint32_t requiredSkinnedVertexCount = (std::max)(1u,
			static_cast<uint32_t>((std::max)(items.size(), size_t(1))) * gpuMesh.vertexCount);
		const uint32_t regenerated = skinning_->EnsureVertexCapacity(requiredSkinnedVertexCount);
		if (regenerated != 0) {
			skinningOutputValid_ = false;
			skinningBufferGeneration_ += regenerated;
		}

		skinningDispatched_ = skinningOutputValid_ &&
			currentSkinningPoseHash_ == dispatchedSkinningPoseHash_;
	}
	subMeshParamGenerations_.assign(subMeshParamScratch_.size(), ++parameterGeneration_);
	batchIdentity_.Capture(batch, items, gpuMesh);
}
