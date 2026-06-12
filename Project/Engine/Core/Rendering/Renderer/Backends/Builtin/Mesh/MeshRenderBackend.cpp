#include "MeshRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommandContext.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Assets/RenderPipelineAsset.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/Draw/VertexMeshDrawPath.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/Draw/MeshShaderDrawPath.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

// c++
#include <cstdint>

//============================================================================
//	MeshRenderBackend classMethods
//============================================================================
namespace {

	void MixHash(uint64_t& hash, uint64_t value) {

		hash ^= value;
		hash *= 1099511628211ull;
	}

	// メッシュ描画に使用するパスをマテリアルから解決する
	bool ResolveMeshPass(const Engine::RenderDrawContext& context, Engine::AssetID requestedMaterialID,
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
		// ZPrepassはデフォルトメッシュマテリアルへフォールバック
		if (context.passKind == Engine::MaterialPassKind::ZPrepass) {
			return Engine::BackendDrawCommon::ResolveMaterialPass(context, Engine::AssetID{},
				Engine::DefaultMaterialSlot::Mesh, { Engine::MaterialPassKind::ZPrepass }, outResolved);
		}
		return false;
	}
}

void Engine::MeshRenderBackend::EnsureInitialized(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}

	// メッシュ描画リソースデータ初期化
	meshResourceManager_.Init(graphicsCore);
	initialized_ = true;
}

Engine::MeshRenderBackend::MeshRenderBackend() {

	// 描画パスの登録
	drawPaths_.emplace_back(std::make_unique<VertexMeshDrawPath>());
	drawPaths_.emplace_back(std::make_unique<MeshShaderDrawPath>());

	// メッシュ固有Graphicsバインドスロットを初期化時に登録する
	viewCBVSlot_         = sharedBindCache_.AddSlot("ViewConstants",          ShaderBindingKind::CBV);
	drawCBVSlot_         = sharedBindCache_.AddSlot("MeshDrawConstants",      ShaderBindingKind::CBV);
	packedVtxSRVSlot_    = sharedBindCache_.AddSlot("gPackedVertices",        ShaderBindingKind::SRV);
	vtxSubMeshSRVSlot_   = sharedBindCache_.AddSlot("gVertexSubMeshIndices",  ShaderBindingKind::SRV);
	skinnedVtxSRVSlot_   = sharedBindCache_.AddSlot("gSkinnedVertices",       ShaderBindingKind::SRV);
	skinnedPkdVtxSRVSlot_= sharedBindCache_.AddSlot("gSkinnedPackedVertices", ShaderBindingKind::SRV);
	meshInstSRVSlot_     = sharedBindCache_.AddSlot("gMeshInstances",         ShaderBindingKind::SRV);
	subMeshSRVSlot_      = sharedBindCache_.AddSlot("gSubMeshes",             ShaderBindingKind::SRV);
	outlineSRVSlot_      = sharedBindCache_.AddSlot("gMeshOutlines",          ShaderBindingKind::SRV);
	screenSpaceOutlineMaskCBVSlot_ = sharedBindCache_.AddSlotByRegister(ShaderBindingKind::CBV, 1, 1);

	// スキニングComputeバインドスロットを初期化時に登録する
	skinConstCBVSlot_     = skinningBindCache_.AddSlot("SkinningConstants",      ShaderBindingKind::CBV);
	inputVtxSRVSlot_      = skinningBindCache_.AddSlot("gInputVertices",         ShaderBindingKind::SRV);
	vtxInflSRVSlot_       = skinningBindCache_.AddSlot("gVertexInfluences",      ShaderBindingKind::SRV);
	skinPaletteSRVSlot_   = skinningBindCache_.AddSlot("gSkinningPalette",       ShaderBindingKind::SRV);
	skinnedVtxUAVSlot_    = skinningBindCache_.AddSlot("gSkinnedVertices",       ShaderBindingKind::UAV);
	skinnedPkdVtxUAVSlot_ = skinningBindCache_.AddSlot("gSkinnedPackedVertices", ShaderBindingKind::UAV);
}

Engine::MeshRenderBackend::~MeshRenderBackend() {

	meshResourceManager_.Finalize();
	resourcePool_.Clear();
	subMeshCBPool_.Clear();
	ClearStaticBatchCache();
	skinnedBatchCache_.clear();
	skinnedSourceLookup_.clear();
	for (auto& drawPath : drawPaths_) {
		drawPath.reset();
	}
	drawPaths_.clear();
	initialized_ = false;
}

void Engine::MeshRenderBackend::ClearStaticBatchCache() {

	// StaticBatchCacheEntry内のunique_ptr<MeshBatchResources>を明示resetしてからキャッシュを破棄する
	for (auto& [key, entry] : staticBatchCache_) {
		(void)key;
		entry.resources.reset();
	}
	staticBatchCache_.clear();
}

void Engine::MeshRenderBackend::RequestMeshes(GraphicsCore& graphicsCore,
	AssetDatabase& assetDatabase, std::span<const AssetID> meshAssets) {

	// 初期化
	EnsureInitialized(graphicsCore);

	for (const AssetID& meshAssetID : meshAssets) {
		if (!meshAssetID) {
			continue;
		}
		meshResourceManager_.RequestMesh(assetDatabase, meshAssetID);
	}
	// このフレーム中に読み込み完了しているものをGPUへ反映
	meshResourceManager_.FlushUploads();
}

void Engine::MeshRenderBackend::PreDispatchSkinningBatch(const RenderDrawContext& context,
	std::span<const RenderItem* const> items) {

	MeshPreparedBatch prepared{};
	// 描画に必要なリソースを準備する
	if (!PrepareBatchResources(context, items, prepared)) {
		return;
	}

	// スキニング対象だけ先にディスパッチして頂点を更新
	DispatchSkinning(context, prepared);
}

bool Engine::MeshRenderBackend::FindSkinnedVertexSource(ECSWorld* world, Entity entity, AssetID mesh,
	SkinnedVertexSource& outSource) const {

	SkinnedSourceLookupKey key{};
	key.world = world;
	key.entity = entity;
	key.mesh = mesh;

	auto it = skinnedSourceLookup_.find(key);
	if (it == skinnedSourceLookup_.end()) {
		return false;
	}
	outSource = it->second;
	return true;
}

void Engine::MeshRenderBackend::BeginFrame(GraphicsCore& graphicsCore) {

	// 初期化処理
	EnsureInitialized(graphicsCore);
	++frameIndex_;

	// スキンメッシュのバッチキャッシュをクリアする
	skinnedBatchCache_.clear();
	skinnedSourceLookup_.clear();
	// 静的メッシュはフレームを跨いで再利用するため、寿命切れだけを落とす
	PruneStaticBatchCache();

	resourcePool_.BeginFrame();
	subMeshCBPool_.BeginFrame();

	meshResourceManager_.BeginFrame(graphicsCore);
	meshResourceManager_.FlushUploads();
}

void Engine::MeshRenderBackend::DrawBatch(const RenderDrawContext& context,
	std::span<const RenderItem* const> items) {

	GraphicsCore& graphicsCore = *context.graphicsCore;
	EnsureInitialized(graphicsCore);

	// 描画に必要な情報を準備する
	MeshPreparedBatch prepared{};
	if (!PrepareBatch(context, items, prepared)) {
		return;
	}
	if (!prepared.pipelineState || !prepared.variant) {
		return;
	}

	// スキニング更新フォロースルー
	DispatchSkinning(context, prepared);

	// パイプラインを設定
	ID3D12GraphicsCommandList6* commandList = BackendDrawCommon::SetupGraphicsPipeline(
		context, *prepared.pipelineState, prepared.items.front()->blendMode);

	// 描画に必要な共通リソースをバインド
	BindSharedResources(context, prepared, commandList);

	IMeshDrawPath& path = SelectDrawPath(*prepared.variant);

	// 経路固有のセットアップ
	MeshPathSetupContext setupContext{};
	setupContext.commandList = commandList;
	setupContext.drawContext = &context;
	setupContext.prepared = &prepared;
	path.Setup(setupContext);
	// 経路固有の描画
	MeshPathDrawContext drawPathContext{};
	drawPathContext.graphicsCore = &graphicsCore;
	drawPathContext.commandList = commandList;
	drawPathContext.drawContext = &context;
	drawPathContext.prepared = &prepared;
	drawPathContext.subMeshCBPool = &subMeshCBPool_;
	path.Draw(drawPathContext);
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
	outPrepared.items.assign(items.begin(), items.end());
	outPrepared.batchMesh = MeshDrawPathCommon::ResolveBatchMesh(*context.batch, items);
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

		// スキニングするメッシュは、同一フレーム内でのみバッチ結果をキャッシュする
		SkinnedBatchCacheKey key{};
		key.world = outPrepared.items.front()->world;
		key.mesh = outPrepared.batchMesh;
		key.hash = BuildBatchHash(outPrepared.items);
		auto it = skinnedBatchCache_.find(key);
		if (it != skinnedBatchCache_.end()) {

			resources = it->second;
			// Viewだけは毎描画で変わるため、キャッシュヒット時も更新する
			resources->UpdateView(*context.view, context.cullingView);
		} else {

			MeshBatchResources& acquired = resourcePool_.Acquire(graphicsCore,
				[](MeshBatchResources& resource, GraphicsCore& core) {
					resource.Init(core);
				});
			// ビュー行列を更新して描画に必要なデータをアップロードする
			acquired.UpdateView(*context.view, context.cullingView);
			acquired.UploadBatchData(context, *context.batch, outPrepared.items, *outPrepared.gpuMesh);

			resources = &acquired;

			// キャッシュに登録する
			skinnedBatchCache_.emplace(key, resources);
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
		key.hash = BuildStaticBatchHash(context, outPrepared.items, *outPrepared.gpuMesh);

		auto it = staticBatchCache_.find(key);
		if (it != staticBatchCache_.end()) {

			resources = it->second.resources.get();
			// SceneView/GameViewで行列が変わるため、キャッシュ済みでもView定数だけ更新する
			resources->UpdateView(*context.view, context.cullingView);
			it->second.lastUsedFrame = frameIndex_;
		} else {

			StaticBatchCacheEntry entry{};
			entry.resources = std::make_unique<MeshBatchResources>();
			entry.resources->Init(graphicsCore);
			entry.resources->UpdateView(*context.view, context.cullingView);
			entry.resources->UploadBatchData(context, *context.batch, outPrepared.items, *outPrepared.gpuMesh);
			entry.lastUsedFrame = frameIndex_;
			// ErrorTexture使用中のバッチは、本テクスチャ読込後に作り直せるよう永続化しない
			entry.persistent = !entry.resources->UsesFallbackTexture();

			resources = entry.resources.get();
			staticBatchCache_.emplace(key, std::move(entry));
		}
	}

	// 描画パスごとに変わる定数(カリング設定やアウトライン情報)は、
	// 静的/スキニングキャッシュヒット時も含め毎描画必ず更新する
	resources->UpdateDrawConstants(context, *outPrepared.gpuMesh);

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
	if (!ResolveMeshPass(context, outPrepared.items.front()->material, resolvedPass)) {
		return false;
	}

	// パイプラインアセットのロード
	const RenderPipelineAsset* pipelineAsset = context.assetLibrary->LoadPipeline(resolvedPass.pass->pipeline);
	if (!pipelineAsset) {
		return false;
	}

	// ランタイムの機能情報に応じたパイプラインバリアントを取得
	const PipelineVariantKind desiredKind = context.forceVertexMeshVariant ?
		PipelineVariantKind::GraphicsVertex :
		resolvedPass.pass->preferredVariant;
	outPrepared.variant = ResolveBestVariant(*pipelineAsset, desiredKind, context.runtimeFeatures);

	// パイプライン取得
	outPrepared.pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(context, *resolvedPass.pass);
	return true;
}

void Engine::MeshRenderBackend::BindSharedResources(const RenderDrawContext& context,
	const MeshPreparedBatch& prepared, ID3D12GraphicsCommandList6* commandList) {

	// バッファレジストリ登録済みバッファをまとめてバインドする
	registryAutoBindTable_.Sync(*prepared.pipelineState, *context.bufferRegistry);
	registryAutoBindTable_.BindGraphics(*context.bufferRegistry, commandList);

	// メッシュ固有バインドのスロット解決を更新
	sharedBindCache_.Sync(*prepared.pipelineState);

	if (sharedBindCache_.Has(viewCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, sharedBindCache_.Get(viewCBVSlot_),
			prepared.resources->GetViewGPUAddress(context.view->kind));
	}
	if (sharedBindCache_.Has(drawCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, sharedBindCache_.Get(drawCBVSlot_),
			prepared.resources->GetDrawGPUAddress());
	}
	if (sharedBindCache_.Has(packedVtxSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, sharedBindCache_.Get(packedVtxSRVSlot_),
			prepared.gpuMesh->packedVertexSRV.buffer->GetResource()->GetGPUVirtualAddress(),
			prepared.gpuMesh->packedVertexSRV.srvGPUHandle);
	}
	if (sharedBindCache_.Has(vtxSubMeshSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, sharedBindCache_.Get(vtxSubMeshSRVSlot_),
			prepared.gpuMesh->vertexSubMeshIndexSRV.buffer->GetResource()->GetGPUVirtualAddress(),
			prepared.gpuMesh->vertexSubMeshIndexSRV.srvGPUHandle);
	}

	// デフォルトは元メッシュ頂点でスキニング済みなら更新後バッファへ差し替える
	D3D12_GPU_VIRTUAL_ADDRESS skinnedVBAddress = prepared.gpuMesh->vertexSRV.buffer->GetResource()->GetGPUVirtualAddress();
	D3D12_GPU_DESCRIPTOR_HANDLE skinnedVBHandle = prepared.gpuMesh->vertexSRV.srvGPUHandle;
	D3D12_GPU_VIRTUAL_ADDRESS skinnedPackedVBAddress = prepared.gpuMesh->packedVertexSRV.buffer->GetResource()->GetGPUVirtualAddress();
	D3D12_GPU_DESCRIPTOR_HANDLE skinnedPackedVBHandle = prepared.gpuMesh->packedVertexSRV.srvGPUHandle;
	if (prepared.resources->HasSkinningResources()) {

		skinnedVBAddress = prepared.resources->GetSkinnedVerticesGPUAddress();
		skinnedVBHandle = prepared.resources->GetSkinnedVerticesSRVHandle();
		skinnedPackedVBAddress = prepared.resources->GetSkinnedPackedVerticesGPUAddress();
		skinnedPackedVBHandle = prepared.resources->GetSkinnedPackedVerticesSRVHandle();
	}
	if (sharedBindCache_.Has(skinnedVtxSRVSlot_) && (skinnedVBAddress != 0 || skinnedVBHandle.ptr != 0)) {
		RootBindingCommand::SetGraphicsSRV(commandList, sharedBindCache_.Get(skinnedVtxSRVSlot_),
			skinnedVBAddress, skinnedVBHandle);
	}
	// MeshShaderは圧縮頂点側も読むため、通常頂点と同じタイミングで差し替える
	if (sharedBindCache_.Has(skinnedPkdVtxSRVSlot_) && (skinnedPackedVBAddress != 0 || skinnedPackedVBHandle.ptr != 0)) {
		RootBindingCommand::SetGraphicsSRV(commandList, sharedBindCache_.Get(skinnedPkdVtxSRVSlot_),
			skinnedPackedVBAddress, skinnedPackedVBHandle);
	}
	if (sharedBindCache_.Has(meshInstSRVSlot_) && prepared.resources->GetInstanceMeshGPUAddress() != 0) {
		RootBindingCommand::SetGraphicsSRV(commandList, sharedBindCache_.Get(meshInstSRVSlot_),
			prepared.resources->GetInstanceMeshGPUAddress(), {});
	}
	if (sharedBindCache_.Has(subMeshSRVSlot_) && prepared.resources->GetSubMeshGPUAddress() != 0) {
		RootBindingCommand::SetGraphicsSRV(commandList, sharedBindCache_.Get(subMeshSRVSlot_),
			prepared.resources->GetSubMeshGPUAddress(), {});
	}
	// 背面法アウトライン用のインスタンス別GPUデータでOutline系パイプラインだけが参照する
	if (sharedBindCache_.Has(outlineSRVSlot_) && prepared.resources->GetOutlineGPUAddress() != 0) {
		RootBindingCommand::SetGraphicsSRV(commandList, sharedBindCache_.Get(outlineSRVSlot_),
			prepared.resources->GetOutlineGPUAddress(), {});
	}
	if (sharedBindCache_.Has(screenSpaceOutlineMaskCBVSlot_) &&
		prepared.resources->GetScreenSpaceOutlineMaskGPUAddress() != 0) {
		RootBindingCommand::SetGraphicsCBV(commandList,
			sharedBindCache_.Get(screenSpaceOutlineMaskCBVSlot_),
			prepared.resources->GetScreenSpaceOutlineMaskGPUAddress());
	}
}

Engine::IMeshDrawPath& Engine::MeshRenderBackend::SelectDrawPath(const PipelineVariantDesc& variant) {

	// サポートされているパスを取得して返す
	for (const auto& path : drawPaths_) {
		if (path->Supports(variant)) {
			return *path;
		}
	}
	Assert::Call(false, "Unsupported mesh draw path");
	// サポートされているパスがない
	return *drawPaths_.front();
}

uint64_t Engine::MeshRenderBackend::BuildBatchHash(std::span<const RenderItem* const> items) const {

	uint64_t h = 1469598103934665603ull;
	for (const RenderItem* item : items) {
		if (!item) {
			continue;
		}
		// 抽出時に計算済みのアイテム内容ハッシュ(entity/material/outline/submesh等)を混ぜる
		MixHash(h, item->contentHash);
	}
	return h;
}

uint64_t Engine::MeshRenderBackend::BuildStaticBatchHash(const RenderDrawContext& context,
	std::span<const RenderItem* const> items, const MeshGPUResource& gpuMesh) const {

	uint64_t h = 1469598103934665603ull;
	// ランタイム機能が変わるとGPUへ渡す定数も変わるためHashに含める
	MixHash(h, static_cast<uint64_t>(items.size()));
	MixHash(h, static_cast<uint64_t>(std::hash<AssetID>{}(gpuMesh.assetID)));
	MixHash(h, context.runtimeFeatures.useFrustumCulling ? 1ull : 0ull);
	MixHash(h, context.runtimeFeatures.useContributionCulling ? 1ull : 0ull);
	MixHash(h, context.runtimeFeatures.useNormalConeCulling ? 1ull : 0ull);
	MixHash(h, context.runtimeFeatures.useMeshShader ? 1ull : 0ull);
	MixHash(h, context.forceVertexMeshVariant ? 1ull : 0ull);
	MixHash(h, context.cullingView && context.cullingView->valid ? 1ull : 0ull);

	for (const RenderItem* item : items) {
		if (!item) {
			continue;
		}
		// 抽出時に計算済みのアイテム内容ハッシュ(entity/material/blendMode/worldMatrix/outline/submesh)を混ぜる
		// component再取得やbyte再走査をここでは行わない
		MixHash(h, item->contentHash);
	}
	return h;
}

void Engine::MeshRenderBackend::PruneStaticBatchCache() {

	static constexpr uint64_t kKeepFrameCount = 180;
	for (auto it = staticBatchCache_.begin(); it != staticBatchCache_.end();) {

		// 使われなくなったバッチやFallbackTexture中のバッチを破棄する
		const bool expired = frameIndex_ > it->second.lastUsedFrame + kKeepFrameCount;
		if (!it->second.persistent || expired) {
			it = staticBatchCache_.erase(it);
		} else {
			++it;
		}
	}
}

void Engine::MeshRenderBackend::DispatchSkinning(const RenderDrawContext& context, const MeshPreparedBatch& prepared) {

	// スキニングしないメッシュの場合は何もしない
	if (!prepared.gpuMesh->isSkinned) {
		return;
	}
	// スキニングを行うインスタンスがない場合
	// スキニングに必要なリソースがない場合
	// すでにスキニング処理をディスパッチしている場合
	if (prepared.resources->GetSkinnedInstanceCount() == 0||
		!prepared.resources->HasSkinningResources()||
		prepared.resources->IsSkinningDispatched()) {
		return;
	}

	GraphicsCore& graphicsCore = *context.graphicsCore;

	// スキニングに必要な入力が揃っていない場合は何もしない
	if (!prepared.gpuMesh->vertexSRV.buffer || !prepared.gpuMesh->skinInfluenceSRV.buffer) {
		return;
	}

	// スキニングパイプラインアセットを読み込む
	if (!skinningPipeline_) {

		// ビルトインPipelineはパスではなく.meta GUIDで固定参照する
		skinningPipeline_ = BuiltinAssets::Pipelines::Skinning;
	}

	// スキニングパイプラインの取得
	const PipelineState* pipelineState = context.pipelineCache->GetORCreate(graphicsCore.GetDXObject(),
		*context.assetLibrary, skinningPipeline_, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
	if (!pipelineState) {
		return;
	}

	DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	ID3D12GraphicsCommandList6* commandList = dxCommand->GetCommandList();

	// スキニング結果の出力先リソースを取得
	ID3D12Resource* output = prepared.resources->GetSkinnedVerticesResource();
	ID3D12Resource* packedOutput = prepared.resources->GetSkinnedPackedVerticesResource();
	if (!output || !packedOutput) {
		return;
	}

	// UAV書き込みへ遷移
	dxCommand->TransitionBarriers({ output }, prepared.resources->GetSkinnedVertexState(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	prepared.resources->SetSkinnedVertexState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	dxCommand->TransitionBarriers({ packedOutput }, prepared.resources->GetSkinnedPackedVertexState(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	prepared.resources->SetSkinnedPackedVertexState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// パイプラインを設定
	commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState(pipelineState->GetComputePipeline());

	// バッファバインドはパイプラインが変わった時だけ再解決する
	skinningBindCache_.Sync(*pipelineState);
	if (skinningBindCache_.Has(skinConstCBVSlot_)) {
		RootBindingCommand::SetComputeCBV(commandList, skinningBindCache_.Get(skinConstCBVSlot_),
			prepared.resources->GetSkinningConstantsGPUAddress());
	}
	if (skinningBindCache_.Has(inputVtxSRVSlot_)) {
		RootBindingCommand::SetComputeSRV(commandList, skinningBindCache_.Get(inputVtxSRVSlot_),
			prepared.gpuMesh->vertexSRV.buffer->GetResource()->GetGPUVirtualAddress(),
			prepared.gpuMesh->vertexSRV.srvGPUHandle);
	}
	if (skinningBindCache_.Has(vtxInflSRVSlot_)) {
		RootBindingCommand::SetComputeSRV(commandList, skinningBindCache_.Get(vtxInflSRVSlot_),
			prepared.gpuMesh->skinInfluenceSRV.buffer->GetResource()->GetGPUVirtualAddress(),
			prepared.gpuMesh->skinInfluenceSRV.srvGPUHandle);
	}
	if (skinningBindCache_.Has(skinPaletteSRVSlot_) && prepared.resources->GetSkinningPaletteGPUAddress() != 0) {
		RootBindingCommand::SetComputeSRV(commandList, skinningBindCache_.Get(skinPaletteSRVSlot_),
			prepared.resources->GetSkinningPaletteGPUAddress(), {});
	}
	if (skinningBindCache_.Has(skinnedVtxUAVSlot_)) {
		RootBindingCommand::SetComputeUAV(commandList, skinningBindCache_.Get(skinnedVtxUAVSlot_),
			prepared.resources->GetSkinnedVerticesGPUAddress(),
			prepared.resources->GetSkinnedVerticesUAVHandle());
	}
	if (skinningBindCache_.Has(skinnedPkdVtxUAVSlot_)) {
		RootBindingCommand::SetComputeUAV(commandList, skinningBindCache_.Get(skinnedPkdVtxUAVSlot_),
			prepared.resources->GetSkinnedPackedVerticesGPUAddress(),
			prepared.resources->GetSkinnedPackedVerticesUAVHandle());
	}

	// スキニング処理をディスパッチ
	// Xは頂点数、Yはスキニング対象インスタンス数
	commandList->Dispatch(DxUtils::RoundUp(prepared.gpuMesh->vertexCount, 256),
		prepared.resources->GetSkinnedInstanceCount(), 1);

	// UAVバリアを挿入して、スキニング結果の書き込み完了を保証する
	dxCommand->UAVBarrier(output);
	dxCommand->UAVBarrier(packedOutput);

	D3D12_RESOURCE_STATES readState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	// スキニング結果をシェーダーリソースとして使用できるように遷移
	dxCommand->TransitionBarriers({ output }, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, readState);
	dxCommand->TransitionBarriers({ packedOutput }, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, readState);

	// スキニング結果のリソース状態を更新して、スキニング処理をディスパッチしたことをセットする
	prepared.resources->SetSkinnedVertexState(readState);
	prepared.resources->SetSkinnedPackedVertexState(readState);
	prepared.resources->SetSkinningDispatched(true);
}

void Engine::MeshRenderBackend::RegisterSkinnedSources(AssetID meshAssetID, MeshBatchResources& resources,
	std::span<const RenderItem* const> items) {

	// スキニング用のリソースがない場合は何もしない
	if (!resources.HasSkinningResources()) {
		return;
	}

	for (const RenderItem* item : items) {
		if (!item || !item->world) {
			continue;
		}

		uint32_t vertexOffset = 0;
		if (!resources.FindSkinnedVertexOffset(item->world, item->entity, vertexOffset)) {
			continue;
		}

		// スキニング頂点の検索テーブルのキーを構築する
		SkinnedSourceLookupKey key{};
		key.world = item->world;
		key.entity = item->entity;
		key.mesh = meshAssetID;

		// スキニング頂点のGPUリソース情報を登録する
		SkinnedVertexSource source{};
		source.gpuAddress = resources.GetSkinnedVerticesGPUAddress();
		source.srvIndex = resources.GetSkinnedVerticesSRVIndex();
		source.vertexOffset = vertexOffset;

		// 同一エンティティが複数回描画される場合は、最後のものが登録される
		skinnedSourceLookup_[key] = source;
	}
}

bool Engine::MeshRenderBackend::CanBatch(const RenderItem& first,
	const RenderItem& next, [[maybe_unused]] const GraphicsRuntimeFeatures& features) const {

	return BackendDrawCommon::CanBatchBasic(first, next);
}
