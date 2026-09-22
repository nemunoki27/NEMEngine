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

}

void Engine::MeshBatchResources::BuildBatchData(const RenderDrawContext& drawContext,
	const RenderSceneBatch& batch, const std::span<const RenderItem* const>& items, const MeshGPUResource& gpuMesh) {

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
		const uint32_t regenerated = skinning_->EnsureVertexCapacity(requiredSkinnedVertexCount);
		if (regenerated != 0) {
			skinningOutputValid_ = false;
			skinningBufferGeneration_ += regenerated;
		}

		skinningDispatched_ = skinningOutputValid_ &&
			currentSkinningPoseHash_ == dispatchedSkinningPoseHash_;
		if (!skinningDispatched_ && 0 < skinnedInstanceCount_) {

			// ポーズ変更時だけパレットとDispatch定数を更新する
			skinning_->Upload(paletteScratch_, gpuMesh.vertexCount, gpuMesh.boneCount, skinnedInstanceCount_);
		}
	}
	subMeshParamGenerations_.assign(subMeshParamScratch_.size(), ++parameterGeneration_);
	CaptureBatchIdentity(batch, items, gpuMesh);
}
