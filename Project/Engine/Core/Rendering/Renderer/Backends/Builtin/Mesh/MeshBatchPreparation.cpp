#include "MeshRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/Draw/VertexMeshDrawPath.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/Draw/MeshShaderDrawPath.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <cstdint>

//============================================================================
//	MeshRenderBackend classMethods
//============================================================================
namespace {

	bool ResolveMeshPass(const Engine::RenderDrawContext& context,
		Engine::AssetID requestedMaterialID,
		Engine::MaterialSurfaceMode surfaceMode,
		Engine::BackendDrawCommon::ResolvedMaterialPass& outResolved) {

		// 背面法アウトラインの3パスは、元マテリアルと切り離してMeshOutlineデフォルトマテリアルから解決する
		// コンポーネントを追加するだけで任意の既存マテリアルへアウトラインを適用できるようにする
		if (context.passKind == Engine::MaterialPassKind::Outline ||
			context.passKind == Engine::MaterialPassKind::OutlineStencilWrite ||
			context.passKind == Engine::MaterialPassKind::OutlineStencilTest) {

			return Engine::BackendDrawCommon::ResolveMaterialPass(
				context,
				Engine::AssetID{},
				Engine::DefaultMaterialSlot::MeshOutline,
				{ context.passKind },
				outResolved);
		}
		if (context.passKind == Engine::MaterialPassKind::ScreenSpaceOutlineMask ||
			context.passKind == Engine::MaterialPassKind::ScreenSpaceOutlineCoverageMask) {

			const Engine::AssetID materialID = Engine::BuiltinAssets::Materials::ScreenSpaceOutlineMask;
			const Engine::MaterialAsset* material = context.assetLibrary->LoadMaterial(materialID);
			if (!material) {
				return false;
			}
			const Engine::MaterialPassBinding* pass = Engine::FindPass(*material, context.passKind);
			if (!pass) {
				return false;
			}
			outResolved.materialID = materialID;
			outResolved.material = material;
			outResolved.pass = pass;
			return true;
		}
		// 通常描画
		if (context.passKind == Engine::MaterialPassKind::Draw) {
			if (surfaceMode == Engine::MaterialSurfaceMode::Masked &&
				Engine::BackendDrawCommon::ResolveMaterialPass(
					context, requestedMaterialID,
					Engine::DefaultMaterialSlot::Mesh,
					{ Engine::MaterialPassKind::Masked }, outResolved)) {
				return true;
			}
			if (Engine::BackendDrawCommon::ResolveMaterialPass(context, requestedMaterialID,
				Engine::DefaultMaterialSlot::Mesh, { Engine::MaterialPassKind::Draw }, outResolved)) {
				return true;
			}
		} else if (context.passKind == Engine::MaterialPassKind::Transparent) {

			if (Engine::BackendDrawCommon::ResolveMaterialPass(context, requestedMaterialID,
				Engine::DefaultMaterialSlot::Mesh, { Engine::MaterialPassKind::Transparent }, outResolved)) {
				return true;
			}
			return Engine::BackendDrawCommon::ResolveMaterialPass(context, Engine::AssetID{},
				Engine::DefaultMaterialSlot::Mesh, { Engine::MaterialPassKind::Transparent }, outResolved);
		} else {
			// Draw以外のパスは、そのパス種別をそのまま探す
			if (Engine::BackendDrawCommon::ResolveMaterialPass(context, requestedMaterialID,
				Engine::DefaultMaterialSlot::Mesh, { context.passKind }, outResolved)) {
				return true;
			}
		}
		return false;
	}
}

bool Engine::MeshRenderBackend::PrepareBatchResources(const RenderDrawContext& context,
	std::span<const RenderItem* const> items, MeshPreparedBatch& outPrepared) {

	if (items.empty()) {
		return false;
	}

	GraphicsCore& graphicsCore = *context.graphicsCore;
	const RenderItem* firstItem = items.front();
	if (!firstItem) {
		return false;
	}

	// 描画に使用するデータを取得
	outPrepared = {};
	outPrepared.items = items;
	outPrepared.batchMesh = MeshDrawPathCommon::ResolveBatchMesh(*context.batch, items);
	const MeshRenderPayload* firstPayload =
		context.batch->GetPayload<MeshRenderPayload>(*firstItem);
	if (!firstPayload) {
		return false;
	}
	outPrepared.subMeshIndex = firstPayload->subMeshIndex;
	outPrepared.subMeshGroupIndex = firstPayload->subMeshGroupIndex;
	// バッチに使用するメッシュがない場合は描画できない
	if (!outPrepared.batchMesh) {
		return false;
	}

	// 既存GPUリソースを取得
	outPrepared.gpuMesh = meshResourceManager_.Find(outPrepared.batchMesh);
	// 未ロードなら読み込み要求して、GPUリソースが利用可能になるまで待つ
	if (!outPrepared.gpuMesh) {

		meshResourceManager_.RequestMesh(*context.assetDatabase, outPrepared.batchMesh);
		outPrepared.gpuMesh = meshResourceManager_.Find(outPrepared.batchMesh);
		if (!outPrepared.gpuMesh) {
			return false;
		}
	}
	if (outPrepared.subMeshIndex != kAllMeshSubMeshes &&
		outPrepared.gpuMesh->subMeshes.size() <= outPrepared.subMeshIndex) {
		return false;
	}

	MeshBatchResources* resources = nullptr;
	bool containsBillboard = false;
	for (const RenderItem* item : outPrepared.items) {
		if (item && RenderBillboard::HasBillboard(*item)) {
			containsBillboard = true;
			break;
		}
	}

	// キャッシュを使用するか
	bool useSkinningCache = outPrepared.gpuMesh->isSkinned && !containsBillboard;
	if (useSkinningCache) {

		// スキニング結果はポーズ世代が変わるまでフレームを跨いで再利用する
		SkinnedBatchCacheKey key{};
		key.world = outPrepared.items.front()->world;
		key.mesh = outPrepared.batchMesh;
		key.hash = BuildBatchHash(outPrepared.items);
		auto it = skinnedBatchCache_.find(key);
		if (it != skinnedBatchCache_.end()) {

			resources = it->second.resources.get();
			resources->UpdateView(*context.view, context.cullingView);
			if (it->second.lastUploadFrame != frameIndex_) {

				resources->UploadBatchData(context, *context.batch,
					outPrepared.items, *outPrepared.gpuMesh);
				it->second.lastUploadFrame = frameIndex_;
			}
			it->second.lastUsedFrame = frameIndex_;
		} else {

			SkinnedBatchCacheEntry entry{};
			entry.resources = std::make_unique<MeshBatchResources>();
			entry.resources->Init(graphicsCore);
			entry.resources->UpdateView(*context.view, context.cullingView);
			entry.resources->UploadBatchData(context, *context.batch,
				outPrepared.items, *outPrepared.gpuMesh);
			entry.lastUsedFrame = frameIndex_;
			entry.lastUploadFrame = frameIndex_;
			resources = entry.resources.get();
			skinnedBatchCache_.emplace(key, std::move(entry));
		}
	} else if (containsBillboard) {

		// BillboardはビューごとにworldMatrixが変わるため、静的/スキニングキャッシュを使い回さない
		MeshBatchResources& acquired = resourcePool_.Acquire(graphicsCore,
			[](MeshBatchResources& resource, GraphicsCore& core) {
				resource.Init(core);
			});
		acquired.UpdateView(*context.view, context.cullingView);
		acquired.UploadBatchData(context, *context.batch, outPrepared.items, *outPrepared.gpuMesh);
		resources = &acquired;
	} else {

		// 静的メッシュはバッチ内容が同じならGPUアップロード済みデータを使い回す
		StaticBatchCacheKey key{};
		key.world = outPrepared.items.front()->world;
		key.mesh = outPrepared.batchMesh;
		key.hash = BuildStaticBatchHash(
			outPrepared.items, *outPrepared.gpuMesh);

		auto it = staticBatchCache_.find(key);
		if (it != staticBatchCache_.end()) {

			resources = it->second.resources.get();
			// SceneView/GameViewで行列が変わるため、キャッシュ済みでもView定数だけ更新する
			resources->UpdateView(*context.view, context.cullingView);
			const uint64_t renderRevision =
				context.batch->GetSourceRenderRevision();
			const uint64_t transformRevision =
				context.batch->GetSourceTransformRevision();
			if (!resources->MatchesBatch(*context.batch, outPrepared.items, *outPrepared.gpuMesh)) {
				resources->UploadBatchData(context, *context.batch, outPrepared.items, *outPrepared.gpuMesh);
				it->second.persistent = !resources->UsesFallbackTexture();
			} else {
				FrameProfiler::GetInstance().AddMeshUpdate(0, 0, 0, 1, 0);
				resources->RefreshMaterialColors();
				if (it->second.renderRevision != renderRevision || it->second.transformRevision != transformRevision) {
					resources->RefreshBatchTransforms(outPrepared.items);
				}
				resources->UploadCachedBatchData();
			}
			it->second.renderRevision = renderRevision;
			it->second.transformRevision = transformRevision;
			it->second.lastUsedFrame = frameIndex_;
		} else {

			StaticBatchCacheEntry entry{};
			entry.resources = std::make_unique<MeshBatchResources>();
			entry.resources->Init(graphicsCore);
			entry.resources->UpdateView(*context.view, context.cullingView);
			entry.resources->UploadBatchData(context, *context.batch, outPrepared.items, *outPrepared.gpuMesh);
			entry.lastUsedFrame = frameIndex_;
			entry.renderRevision =
				context.batch->GetSourceRenderRevision();
			entry.transformRevision =
				context.batch->GetSourceTransformRevision();
			// ErrorTexture使用中のバッチは、本テクスチャ読込後に作り直せるよう永続化しない
			entry.persistent = !entry.resources->UsesFallbackTexture();

			resources = entry.resources.get();
			staticBatchCache_.emplace(key, std::move(entry));
		}
	}

	// インスタンス数を設定
	outPrepared.instanceCount = resources->GetInstanceCount();
	if (outPrepared.instanceCount == 0) {
		return false;
	}
	outPrepared.resources = resources;

	// スキニング済み頂点の検索テーブルを構築
	if (outPrepared.gpuMesh->isSkinned && resources->HasSkinningResources()) {

		RegisterSkinnedSources(outPrepared.batchMesh, *resources, outPrepared.items);
	}
	return true;
}

bool Engine::MeshRenderBackend::PrepareBatch(const RenderDrawContext& context,
	std::span<const RenderItem* const> items, MeshPreparedBatch& outPrepared) {

	// 描画に必要なリソースを準備する
	if (!PrepareBatchResources(context, items, outPrepared)) {
		return false;
	}

	// マテリアルパスの解決
	BackendDrawCommon::ResolvedMaterialPass resolvedPass{};
	if (!ResolveMeshPass(context, outPrepared.items.front()->material,
		outPrepared.items.front()->surfaceMode, resolvedPass)) {
		return false;
	}

	// パイプライン取得と同時に解決済みバリアントを受け取り、パイプラインアセットの再ロードとバリアント再解決を避ける
	outPrepared.pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(
		context, *resolvedPass.pass, &outPrepared.variant);
	if (!outPrepared.pipelineState) {
		return false;
	}
	// MaterialParameters cbufferへ詰めるため解決済みマテリアルを保持する
	outPrepared.material = resolvedPass.material;
	if (context.passKind == MaterialPassKind::Outline ||
		context.passKind == MaterialPassKind::OutlineStencilWrite ||
		context.passKind == MaterialPassKind::OutlineStencilTest ||
		context.passKind == MaterialPassKind::ScreenSpaceOutlineMask ||
		context.passKind == MaterialPassKind::ScreenSpaceOutlineCoverageMask) {

		// 専用パスでも元マテリアルの頂点変位を引き継ぎ、輪郭と本体を一致させる
		const MaterialAsset* sourceMaterial =
			context.assetLibrary->LoadMaterial(
				outPrepared.items.front()->material);
		if (sourceMaterial) {
			outPrepared.material = sourceMaterial;
		}
	}
	// マテリアル依存の頂点変位を含む描画定数はパス解決後に毎描画更新する
	outPrepared.resources->UpdateDrawConstants(
		context, *outPrepared.gpuMesh, outPrepared.subMeshIndex,
		outPrepared.subMeshGroupIndex, outPrepared.material);
	return true;
}
