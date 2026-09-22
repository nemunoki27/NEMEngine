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

	using Engine::Algorithm::HashCombine;

	// メッシュ描画に使用するパスをマテリアルから解決する

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
	// スキニングComputeバインドスロットを初期化時に登録する
}

Engine::MeshRenderBackend::~MeshRenderBackend() {

	meshResourceManager_.Finalize();
	resourcePool_.Clear();
	ClearWorldBatchCaches();
	for (auto& drawPath : drawPaths_) {
		drawPath.reset();
	}
	drawPaths_.clear();
	initialized_ = false;
}

void Engine::MeshRenderBackend::ClearWorldBatchCaches() {

	// StaticBatchCacheEntry内のunique_ptr<MeshBatchResources>を明示resetしてからキャッシュを破棄する
	for (auto& entry : staticBatchCache_) {
		entry.second.resources.reset();
	}
	staticBatchCache_.clear();
	ClearSkinnedBatchCache();
	skinnedSourceLookup_.clear();
}

void Engine::MeshRenderBackend::ClearSkinnedBatchCache() {

	for (auto& entry : skinnedBatchCache_) {
		entry.second.resources.reset();
	}
	skinnedBatchCache_.clear();
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

bool Engine::MeshRenderBackend::AreMeshesReady(
	std::span<const AssetID> meshAssets) const {

	for (const AssetID meshAssetID : meshAssets) {
		if (meshAssetID && !meshResourceManager_.Find(meshAssetID)) {
			return false;
		}
	}
	return true;
}

void Engine::MeshRenderBackend::PreloadMeshes(GraphicsCore& graphicsCore,
	AssetDatabase& assetDatabase, std::span<const AssetID> meshAssets) {

	EnsureInitialized(graphicsCore);
	for (const AssetID& meshAssetID : meshAssets) {
		meshResourceManager_.RequestMesh(assetDatabase, meshAssetID);
	}
	meshResourceManager_.WaitAll();
}

void Engine::MeshRenderBackend::RequestMeshReload(AssetID meshAssetID) {

	if (!meshAssetID) {
		return;
	}

	// メッシュを破棄して再インポートし、旧gpuMeshを参照していたバッチキャッシュを作り直させる
	// バッチは毎フレームgpuMeshを引き直すので、キャッシュclearで新しいリソースとサブメッシュ構成に追従する
	meshResourceManager_.RequestReload(meshAssetID);
	ClearWorldBatchCaches();
}

void Engine::MeshRenderBackend::PreDispatchSkinningBatch(const RenderDrawContext& context,
	std::span<const RenderItem* const> items) {

	if (items.empty() || !context.batch || !context.assetDatabase) {
		return;
	}

	const AssetID batchMesh = MeshDrawPathCommon::ResolveBatchMesh(*context.batch, items);
	if (!batchMesh) {
		return;
	}
	const MeshGPUResource* gpuMesh = meshResourceManager_.Find(batchMesh);
	if (!gpuMesh) {

		meshResourceManager_.RequestMesh(*context.assetDatabase, batchMesh);
		gpuMesh = meshResourceManager_.Find(batchMesh);
	}
	// 静的メッシュはスキニング用バッチリソースを準備しない
	if (!gpuMesh || !gpuMesh->isSkinned) {
		return;
	}

	MeshPreparedBatch prepared{};
	// 描画に必要なリソースを準備する
	if (!PrepareBatchResources(context, items, prepared)) {
		return;
	}

	// スキニング対象だけ先にディスパッチして頂点を更新
	skinningDispatcher_.Dispatch(context, prepared);
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

	// 前フレームの検索結果だけをクリアし、スキニング結果バッファは世代比較のため保持する
	skinnedSourceLookup_.clear();
	// 静的メッシュはフレームを跨いで再利用するため、寿命切れだけを落とす
	PruneStaticBatchCache();
	PruneSkinnedBatchCache();

	resourcePool_.BeginFrame();
	graphicsBinding_.BeginFrame();
	// マテリアルパラメータCBVのアップロード位置を戻す

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
	skinningDispatcher_.Dispatch(context, prepared);

	// パイプラインを設定
	ID3D12GraphicsCommandList6* commandList = BackendDrawCommon::SetupGraphicsPipeline(
		context, *prepared.pipelineState, prepared.items.front()->blendMode);

	// 描画に必要な共通リソースをバインド
	graphicsBinding_.Bind(context, prepared, commandList);

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
	path.Draw(drawPathContext);
}

Engine::IMeshDrawPath& Engine::MeshRenderBackend::SelectDrawPath(const PipelineVariantDesc& variant) {

	// サポートされているパスを取得して返す
	for (const auto& path : drawPaths_) {
		if (path->Supports(variant)) {
			return *path;
		}
	}
	Assert::Call(false, "未対応のMesh描画経路です");
	// サポートされているパスがない
	return *drawPaths_.front();
}

uint64_t Engine::MeshRenderBackend::BuildBatchHash(std::span<const RenderItem* const> items) const {

	uint64_t h = 1469598103934665603ull;
	for (const RenderItem* item : items) {
		if (!item) {
			continue;
		}
		// 行列やアニメーションPoseは含めず、同じ出力領域を使えるバッチ構成だけを混ぜる
		HashCombine(h, item->entity.index);
		HashCombine(h, item->entity.generation);
		HashCombine(h, static_cast<uint64_t>(std::hash<AssetID>{}(item->material)));
		HashCombine(h, item->batchKey);
		HashCombine(h, static_cast<uint64_t>(item->renderPhase));
		HashCombine(h, static_cast<uint64_t>(item->blendMode));
	}
	return h;
}

uint64_t Engine::MeshRenderBackend::BuildStaticBatchHash(
	std::span<const RenderItem* const> items,
	const MeshGPUResource& gpuMesh) const {

	uint64_t h = 1469598103934665603ull;
	// バッチ識別子だけを使い、Render/Transform世代変更時も同じGPUリソースを再利用する
	HashCombine(h, static_cast<uint64_t>(items.size()));
	HashCombine(h, static_cast<uint64_t>(std::hash<AssetID>{}(gpuMesh.assetID)));
	// 透明ソートで中央の順序だけが変わる場合も別のバッチとして識別する
	HashCombine(h, BuildBatchHash(items));
	return h;
}

void Engine::MeshRenderBackend::PruneStaticBatchCache() {

	static constexpr uint64_t kKeepFrameCount =
		kGraphicsFrameContextCount;
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

void Engine::MeshRenderBackend::PruneSkinnedBatchCache() {

	static constexpr uint64_t kKeepFrameCount =
		kGraphicsFrameContextCount;
	for (auto it = skinnedBatchCache_.begin();
		it != skinnedBatchCache_.end();) {

		const bool expired =
			frameIndex_ > it->second.lastUsedFrame + kKeepFrameCount;
		if (expired) {
			it = skinnedBatchCache_.erase(it);
		} else {
			++it;
		}
	}
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
		source.bufferGeneration = resources.GetSkinningBufferGeneration();
		if (const SkinnedAnimationRuntimeData* runtime =
			TryGetSkinnedAnimationRuntime(*item->world, item->entity)) {
			source.poseGeneration = runtime->poseGeneration;
		}

		// 同一エンティティが複数回描画される場合は、最後のものが登録される
		skinnedSourceLookup_[key] = source;
	}
}

bool Engine::MeshRenderBackend::CanBatch(const RenderItem& first,
	const RenderItem& next, [[maybe_unused]] const GraphicsRuntimeFeatures& features) const {

	return BackendDrawCommon::CanBatchBasic(first, next);
}

//============================================================================
//	MeshRenderBackend classMethods
//============================================================================

namespace Engine {

	bool MeshRenderBackend::SkinnedBatchCacheKey::operator==(const SkinnedBatchCacheKey& rhs) const noexcept {

		return world == rhs.world && mesh == rhs.mesh && hash == rhs.hash;
	}

	size_t MeshRenderBackend::SkinnedBatchCacheKeyHash::operator()(const SkinnedBatchCacheKey& key) const noexcept {

		size_t h = std::hash<void*>{}(key.world);
		h ^= (std::hash<AssetID>{}(key.mesh) << 1);
		h ^= (std::hash<uint64_t>{}(key.hash) << 2);
		return h;
	}

	bool MeshRenderBackend::SkinnedSourceLookupKey::operator==(const SkinnedSourceLookupKey& rhs) const noexcept {

		return world == rhs.world && entity.index == rhs.entity.index &&
			entity.generation == rhs.entity.generation && mesh == rhs.mesh;
	}

	size_t MeshRenderBackend::SkinnedSourceLookupKeyHash::operator()(const SkinnedSourceLookupKey& key) const noexcept {

		size_t h = std::hash<void*>{}(key.world);
		h ^= (std::hash<uint32_t>{}(key.entity.index) << 1);
		h ^= (std::hash<uint32_t>{}(key.entity.generation) << 2);
		h ^= (std::hash<AssetID>{}(key.mesh) << 3);
		return h;
	}

	bool MeshRenderBackend::StaticBatchCacheKey::operator==(const StaticBatchCacheKey& rhs) const noexcept {

		return world == rhs.world && mesh == rhs.mesh && hash == rhs.hash;
	}

	size_t MeshRenderBackend::StaticBatchCacheKeyHash::operator()(const StaticBatchCacheKey& key) const noexcept {

		size_t h = std::hash<void*>{}(key.world);
		h ^= (std::hash<AssetID>{}(key.mesh) << 1);
		h ^= (std::hash<uint64_t>{}(key.hash) << 2);
		return h;
	}
}
