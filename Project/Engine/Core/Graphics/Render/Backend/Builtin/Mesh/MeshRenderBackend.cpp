#include "MeshRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/DxObject/DxCommand.h>
#include <Engine/Core/Graphics/Pipeline/PipelineState.h>
#include <Engine/Core/Graphics/Pipeline/PipelineStateCache.h>
#include <Engine/Core/Graphics/Pipeline/Bind/GraphicsRootBinder.h>
#include <Engine/Core/Graphics/Asset/RenderAssetLibrary.h>
#include <Engine/Core/Graphics/Asset/RenderPipelineAsset.h>
#include <Engine/Core/Graphics/DxLib/DxUtils.h>
#include <Engine/Core/Graphics/Material/MaterialResolver.h>
#include <Engine/Core/Graphics/Render/Backend/Common/BackendDrawCommon.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/Draw/VertexMeshDrawPath.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/Draw/MeshShaderDrawPath.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/Asset/AssetDatabase.h>
#include <Engine/Debug/Assert.h>

//============================================================================
//	MeshRenderBackend classMethods
//============================================================================

namespace {

	// メッシュ描画に使用するパスをマテリアルから解決する
	bool ResolveMeshPass(const Engine::RenderDrawContext& context, Engine::AssetID requestedMaterialID,
		Engine::BackendDrawCommon::ResolvedMaterialPass& outResolved) {

		// 通常描画
		if (context.passName == "Draw") {
			if (Engine::BackendDrawCommon::ResolveMaterialPass(context, requestedMaterialID,
				Engine::DefaultMaterialSlot::Mesh, { "Draw", "Mesh" }, outResolved)) {
				return true;
			}
		} else {
			// "Draw以外のパスは、そのパス名をそのまま探す
			if (Engine::BackendDrawCommon::ResolveMaterialPass(context, requestedMaterialID,
				Engine::DefaultMaterialSlot::Mesh, { context.passName }, outResolved)) {
				return true;
			}
		}
		// ZPrepassはデフォルトメッシュマテリアルへフォールバック
		if (context.passName == "ZPrepass") {
			return Engine::BackendDrawCommon::ResolveMaterialPass(context, Engine::AssetID{},
				Engine::DefaultMaterialSlot::Mesh, { "ZPrepass" }, outResolved);
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

	// スキンメッシュのバッチキャッシュをクリアする
	skinnedBatchCache_.clear();
	skinnedSourceLookup_.clear();

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
	path.Setup(setupContext, bindScratch_);
	// 経路固有の描画
	MeshPathDrawContext drawPathContext{};
	drawPathContext.graphicsCore = &graphicsCore;
	drawPathContext.commandList = commandList;
	drawPathContext.drawContext = &context;
	drawPathContext.prepared = &prepared;
	drawPathContext.subMeshCBPool = &subMeshCBPool_;
	drawPathContext.subMeshBindScratch = &subMeshBindScratch_;
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

	// キャッシュを使用するか
	bool useSkinningCache = outPrepared.gpuMesh->isSkinned;
	if (useSkinningCache) {

		// スキニングするメッシュは、同一フレーム内でのみバッチ結果をキャッシュする
		SkinnedBatchCacheKey key{};
		key.world = outPrepared.items.front()->world;
		key.mesh = outPrepared.batchMesh;
		key.hash = BuildBatchHash(outPrepared.items);
		auto it = skinnedBatchCache_.find(key);
		if (it != skinnedBatchCache_.end()) {

			resources = it->second;
			resources->UpdateView(*context.view);
		} else {

			MeshBatchResources& acquired = resourcePool_.Acquire(graphicsCore,
				[](MeshBatchResources& resource, GraphicsCore& core) {
					resource.Init(core);
				});
			// ビュー行列を更新して描画に必要なデータをアップロードする
			acquired.UpdateView(*context.view);
			acquired.UploadBatchData(context, *context.batch, outPrepared.items, *outPrepared.gpuMesh);

			resources = &acquired;

			// キャッシュに登録する
			skinnedBatchCache_.emplace(key, resources);
		}
	} else {

		MeshBatchResources& acquired = resourcePool_.Acquire(graphicsCore,
			[](MeshBatchResources& resource, GraphicsCore& core) {
				resource.Init(core);
			});
		// ビュー行列を更新して描画に必要なデータをアップロードする
		acquired.UpdateView(*context.view);
		acquired.UploadBatchData(context, *context.batch, outPrepared.items, *outPrepared.gpuMesh);
		resources = &acquired;
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
	if (!ResolveMeshPass(context, outPrepared.items.front()->material, resolvedPass)) {
		return false;
	}

	// パイプラインアセットのロード
	const RenderPipelineAsset* pipelineAsset = context.assetLibrary->LoadPipeline(resolvedPass.pass->pipeline);
	if (!pipelineAsset) {
		return false;
	}

	// GPUランタイム機能を取得
	const auto& runtimeFeatures = context.graphicsCore->GetDXObject().GetFeatureController().GetRuntimeFeatures();
	// ランタイムの機能情報に応じたパイプラインバリアントを取得
	outPrepared.variant = ResolveBestVariant(*pipelineAsset, resolvedPass.pass->preferredVariant, runtimeFeatures);

	// パイプライン取得
	outPrepared.pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(context, *resolvedPass.pass);
	return true;
}

void Engine::MeshRenderBackend::BindSharedResources(const RenderDrawContext& context,
	const MeshPreparedBatch& prepared, ID3D12GraphicsCommandList6* commandList) {

	// バッファバインディングの準備
	BackendDrawCommon::PrepareGraphicsBindItemsScratch(
		*context.bufferRegistry, 11, 0, bindScratch_);
	// 登録されている共通バッファをバインドに追加
	BackendDrawCommon::AppendGraphicsBufferBindings(*context.bufferRegistry,
		*prepared.pipelineState, bindScratch_);
	// ViewConstants
	BackendDrawCommon::AppendGraphicsCBV(*prepared.pipelineState, prepared.resources->GetViewBindingName(),
		prepared.resources->GetViewGPUAddress(context.view->kind), bindScratch_);
	// DrawConstants
	BackendDrawCommon::AppendGraphicsCBV(*prepared.pipelineState,
		prepared.resources->GetDrawBindingName(),
		prepared.resources->GetDrawGPUAddress(), bindScratch_);
	// VertexBuffer
	BackendDrawCommon::AppendGraphicsSRV(*prepared.pipelineState, "gVertices",
		prepared.gpuMesh->vertexSRV.buffer->GetResource()->GetGPUVirtualAddress(),
		prepared.gpuMesh->vertexSRV.srvGPUHandle, bindScratch_);
	BackendDrawCommon::AppendGraphicsSRV(*prepared.pipelineState, "gVertexSubMeshIndices",
		prepared.gpuMesh->vertexSubMeshIndexSRV.buffer->GetResource()->GetGPUVirtualAddress(),
		prepared.gpuMesh->vertexSubMeshIndexSRV.srvGPUHandle, bindScratch_);
	// SkinnedVertexBuffer
	D3D12_GPU_VIRTUAL_ADDRESS skinnedVBAddress = prepared.gpuMesh->vertexSRV.buffer->GetResource()->GetGPUVirtualAddress();
	D3D12_GPU_DESCRIPTOR_HANDLE skinnedVBHandle = prepared.gpuMesh->vertexSRV.srvGPUHandle;
	if (prepared.resources->HasSkinningResources()) {

		skinnedVBAddress = prepared.resources->GetSkinnedVerticesGPUAddress();
		skinnedVBHandle = prepared.resources->GetSkinnedVerticesSRVHandle();
	}
	BackendDrawCommon::AppendGraphicsSRV(*prepared.pipelineState,
		prepared.resources->GetSkinnedVerticesBindingName(),
		skinnedVBAddress, skinnedVBHandle, bindScratch_);
	// MeshInstances
	BackendDrawCommon::AppendGraphicsSRV(*prepared.pipelineState,
		prepared.resources->GetInstanceMeshBindingName(), prepared.resources->GetInstanceMeshGPUAddress(),
		{}, bindScratch_);
	// PSInstances
	BackendDrawCommon::AppendGraphicsSRV(*prepared.pipelineState,
		prepared.resources->GetInstancePSBindingName(), prepared.resources->GetInstancePSGPUAddress(),
		{}, bindScratch_);
	// SubMeshShaderData
	BackendDrawCommon::AppendGraphicsSRV(*prepared.pipelineState,
		prepared.resources->GetSubMeshBindingName(),
		prepared.resources->GetSubMeshGPUAddress(),
		{}, bindScratch_);

	// バインド
	GraphicsRootBinder binder{ *prepared.pipelineState };
	binder.Bind(commandList, bindScratch_);
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
	auto mix = [&h](uint64_t v) {
		h ^= v;
		h *= 1099511628211ull;
		};
	for (const RenderItem* item : items) {
		if (!item) {
			continue;
		}
		mix(item->entity.index);
		mix(item->entity.generation);
	}
	return h;
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

		skinningPipeline_ = context.assetDatabase->ImportOrGet(
			"Engine/Assets/Pipelines/Builtin/Mesh/skinning.pipeline.json", AssetType::RenderPipeline);
	}

	// スキニングパイプラインの取得
	const PipelineState* pipelineState = context.pipelineCache->GetORCreate(graphicsCore.GetDXObject(),
		*context.assetLibrary, skinningPipeline_, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);

	DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	ID3D12GraphicsCommandList6* commandList = dxCommand->GetCommandList();

	// スキニング結果の出力先リソースを取得
	ID3D12Resource* output = prepared.resources->GetSkinnedVerticesResource();
	if (!output) {
		return;
	}

	// UAV書き込みへ遷移
	dxCommand->TransitionBarriers({ output }, prepared.resources->GetSkinnedVertexState(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	prepared.resources->SetSkinnedVertexState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

	// パイプラインを設定
	commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState(pipelineState->GetComputePipeline());

	// バッファバインド
	computeBindScratch_.clear();
	computeBindScratch_.reserve(5);
	computeBindScratch_.push_back({
		prepared.resources->GetSkinningConstantsBindingName(),
		ComputeBindValueType::CBV,
		prepared.resources->GetSkinningConstantsGPUAddress()
		});
	computeBindScratch_.push_back({
		"gInputVertices",
		ComputeBindValueType::SRV,
		prepared.gpuMesh->vertexSRV.buffer->GetResource()->GetGPUVirtualAddress(),
		prepared.gpuMesh->vertexSRV.srvGPUHandle
		});
	computeBindScratch_.push_back({
		"gVertexInfluences",
		ComputeBindValueType::SRV,
		prepared.gpuMesh->skinInfluenceSRV.buffer->GetResource()->GetGPUVirtualAddress(),
		prepared.gpuMesh->skinInfluenceSRV.srvGPUHandle
		});
	computeBindScratch_.push_back({
		prepared.resources->GetSkinningPaletteBindingName(),
		ComputeBindValueType::SRV,
		prepared.resources->GetSkinningPaletteGPUAddress(),
		{}
		});
	computeBindScratch_.push_back({
		prepared.resources->GetSkinnedVerticesBindingName(),
		ComputeBindValueType::UAV,
		prepared.resources->GetSkinnedVerticesGPUAddress(),
		prepared.resources->GetSkinnedVerticesUAVHandle()
		});
	// バインド
	ComputeRootBinder binder{ *pipelineState };
	binder.Bind(commandList, computeBindScratch_);

	// スキニング処理をディスパッチ
	commandList->Dispatch(DxUtils::RoundUp(prepared.gpuMesh->vertexCount, 256),
		prepared.resources->GetSkinnedInstanceCount(), 1);

	// UAVバリアを挿入して、スキニング結果の書き込み完了を保証する
	dxCommand->UAVBarrier(output);

	D3D12_RESOURCE_STATES readState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	// スキニング結果をシェーダーリソースとして使用できるように遷移
	dxCommand->TransitionBarriers({ output }, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, readState);

	// スキニング結果のリソース状態を更新して、スキニング処理をディスパッチしたことをセットする
	prepared.resources->SetSkinnedVertexState(readState);
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