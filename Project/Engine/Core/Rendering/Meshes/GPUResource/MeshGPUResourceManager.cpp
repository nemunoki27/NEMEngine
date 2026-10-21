#include "MeshGPUResourceManager.h"

//============================================================================
//	include
//============================================================================
#include "MeshGPUBuilder.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>

// c++
#include <algorithm>
#include <cmath>
#include <exception>
#include <span>
#include <stdexcept>
#include <utility>

//============================================================================
//	MeshGPUResourceManager classMethods
//============================================================================

Engine::MeshGPUResourceManager::~MeshGPUResourceManager() {

	Finalize();
}

void Engine::MeshGPUResourceManager::Init(GraphicsCore& graphicsCore) {

	Init(graphicsCore.GetDXObject().GetDevice(), graphicsCore.GetBufferUploadService(), graphicsCore.GetSRVDescriptor());
}

void Engine::MeshGPUResourceManager::Init(ID3D12Device* device, BufferUploadService& uploads, SRVDescriptor& descriptors) {

	// すでに初期化されている場合は何もしない
	if (initialized_) {
		return;
	}

	device_ = device;
	srvDescriptor_ = &descriptors;
	uploadService_ = &uploads;
	cacheAdapter_ = DxUtils::FindDeviceAdapter(device);

	// メッシュインポートサービスの初期化
	importService_.Init(4);
	initialized_ = true;
}

void Engine::MeshGPUResourceManager::Finalize() {

	if (!initialized_) {
		return;
	}

	importService_.Finalize();
	for (auto& pending : pendingMeshes_) uploadService_->CancelOwnedUploads(pending.ticket);
	pendingMeshes_.clear();
	{
		std::scoped_lock lock(mutex_);
		for (auto& [id, mesh] : gpuMeshes_) {
			ReleaseMeshResource(mesh);
		}
		// 所有元の終了も描画側のcacheへ伝える
		if (!gpuMeshes_.empty()) {
			++resourceRevision_;
		}
		gpuMeshes_.clear();
		meshAccessTimes_.clear();
		requested_.clear();
		requestRevisions_.clear();
		layoutReloadRevisions_.clear();
	}
	device_ = nullptr;
	srvDescriptor_ = nullptr;
	uploadService_ = nullptr;
	assetDatabase_ = nullptr;
	cacheAdapter_.Reset();
	cacheCollectionTime_ = {};
	initialized_ = false;
}

void Engine::MeshGPUResourceManager::BeginFrame(GraphicsCore& graphicsCore) {

	uploadedThisFrame_ = 0;

	// 初期化されていない場合は初期化する
	if (!initialized_) {

		Init(graphicsCore);
	}
	CollectUnusedMeshes();
	PublishReadyMeshes();
}

void Engine::MeshGPUResourceManager::RequestMesh(AssetDatabase& assetDatabase, AssetID meshAssetID) {

	assetDatabase_ = &assetDatabase;

	// 無効なIDは無視
	if (!meshAssetID) {
		return;
	}

	std::scoped_lock lock(mutex_);
	meshAccessTimes_[meshAssetID] = std::chrono::steady_clock::now();
	if (gpuMeshes_.contains(meshAssetID) || requested_.contains(meshAssetID)) {
		return;
	}
	BeginRequest(meshAssetID);
}

void Engine::MeshGPUResourceManager::RequestReload(AssetID meshAssetID) {

	if (!meshAssetID || !assetDatabase_) {
		return;
	}

	std::scoped_lock lock(mutex_);
	// 初回読込中や失敗後も、要求済みAssetなら最新内容を受け付ける
	if (!requestRevisions_.contains(meshAssetID)) {
		return;
	}
	BeginRequest(meshAssetID);
	// 内容の再読込とGPU Cacheの再生成を区別する
	layoutReloadRevisions_[meshAssetID] = requestRevisions_.at(meshAssetID);
}

void Engine::MeshGPUResourceManager::ReleaseMeshResource(MeshGPUResource& mesh) {

	MeshGPUBuilder::Release(mesh);
}

void Engine::MeshGPUResourceManager::FlushUploads(bool unlimited) {

	PublishReadyMeshes();
	// 読み込み待ちのメッシュアセットのうち、GPUにアップロードされていないものをアップロードする
	std::vector<std::pair<AssetID, uint64_t>> pending{};
	{
		std::scoped_lock lock(mutex_);
		pending.reserve(requested_.size());
		for (const auto& request : requested_) {
			pending.emplace_back(request);
		}
	}

	for (const auto& [id, revision] : pending) {

		if (!unlimited && uploadedThisFrame_ >= uploadLimit_) { break; }

		ImportedMeshAsset imported{};
		if (!importService_.TakeImported(id, imported)) {
			if (importService_.ConsumeFailed(id)) {
				// 失敗時は旧表示を残し、次回要求を受け付けられる状態へ戻す
				Logger::Output(LogType::Engine, spdlog::level::err,
					"Meshの読み込みに失敗しました asset={}", ToString(id));
				CompleteRequest(id, revision);
			}
			continue;
		}

		++uploadedThisFrame_;
		// GPUにアップロード
		try {
			if (UploadImported(std::move(imported), revision)) continue;
		} catch (...) {
			CompleteRequest(id, revision);
			throw;
		}
		CompleteRequest(id, revision);
	}
}

void Engine::MeshGPUResourceManager::WaitAll() {

	for (;;) {
		importService_.WaitAll();
		FlushUploads(true);
		uploadService_->FlushAndWait();
		// 完了時に再投入した最新要求も待つ
		std::scoped_lock lock(mutex_);
		if (requested_.empty()) {
			return;
		}
	}
}

const Engine::MeshGPUResource* Engine::MeshGPUResourceManager::Find(AssetID meshAssetID) const {

	std::scoped_lock lock(mutex_);
	auto it = gpuMeshes_.find(meshAssetID);
	if (it == gpuMeshes_.end()) {
		return nullptr;
	}
	meshAccessTimes_[meshAssetID] = std::chrono::steady_clock::now();
	return &it->second;
}

void Engine::MeshGPUResourceManager::CollectUnusedMeshes() {

	const auto now = std::chrono::steady_clock::now();
	if (!cacheAdapter_ || now - cacheCollectionTime_ < std::chrono::seconds(1)) return;
	cacheCollectionTime_ = now;
	DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
	if (FAILED(cacheAdapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)) ||
		memory.Budget == 0 || double(memory.CurrentUsage) <= double(memory.Budget) * 0.8) return;
	std::scoped_lock lock(mutex_);
	std::vector<AssetID> unused;
	// 描画とTLASが参照する形状は回収しない
	for (const auto& [id, mesh] : gpuMeshes_) {

		if (requested_.contains(id) || !mesh.ownership || mesh.ownership.use_count() != 1) continue;
		const auto access = meshAccessTimes_.find(id);
		if (access != meshAccessTimes_.end() && now - access->second >= std::chrono::seconds(5)) unused.push_back(id);
	}
	std::sort(unused.begin(), unused.end(), [&](AssetID left, AssetID right) {
		return meshAccessTimes_.at(left) < meshAccessTimes_.at(right);
	});
	uint64_t releasedBytes = 0;
	for (AssetID id : unused) {

		auto found = gpuMeshes_.find(id);
		auto& mesh = found->second;
		const auto allocation = [&](const auto& handle) {

			if (!handle.buffer) return uint64_t(0);
			const auto description = handle.buffer->GetResource()->GetDesc();
			return device_->GetResourceAllocationInfo(0, 1, &description).SizeInBytes;
		};
		releasedBytes += allocation(mesh.vertexSRV) + allocation(mesh.packedVertexSRV) + allocation(mesh.indexSRV) +
			allocation(mesh.vertexSubMeshIndexSRV) + allocation(mesh.primitiveSubMeshIndexSRV) + allocation(mesh.meshletDrawSRV) +
			allocation(mesh.meshletBoundsSRV) + allocation(mesh.meshletVertexIndexSRV) + allocation(mesh.packedMeshletVertexIndexSRV) +
			allocation(mesh.meshletPrimitiveIndexSRV) + allocation(mesh.skinInfluenceSRV);
		if (mesh.indexBuffer.GetResource() && (!mesh.indexSRV.buffer ||
			mesh.indexBuffer.GetResource() != mesh.indexSRV.buffer->GetResource())) {

			const auto description = mesh.indexBuffer.GetResource()->GetDesc();
			releasedBytes += device_->GetResourceAllocationInfo(0, 1, &description).SizeInBytes;
		}
		// BufferとDescriptorは既存のFence回収へ渡す
		ReleaseMeshResource(mesh);
		gpuMeshes_.erase(found);
		meshAccessTimes_.erase(id);
		++resourceRevision_;
		if (double(memory.CurrentUsage) - double(releasedBytes) <= double(memory.Budget) * 0.7) break;
	}
	if (releasedBytes > 0) {

		Logger::Output(LogType::Engine, "[MeshCache] 未使用形状を回収しました bytes={} usage={} budget={}",
			releasedBytes, memory.CurrentUsage, memory.Budget);
	}
}

void Engine::MeshGPUResourceManager::BeginRequest(AssetID asset) {

	auto& revision = requestRevisions_[asset];
	if (revision == UINT64_MAX) {
		throw std::overflow_error("Meshの要求世代が上限に達しました");
	}
	++revision;
	if (!requested_.contains(asset)) {
		QueueLatestRequest(asset, revision);
	}
}

void Engine::MeshGPUResourceManager::QueueLatestRequest(AssetID asset, uint64_t revision) {

	// workerの開始前に受付状態を確保する
	const auto [entry, inserted] = requested_.emplace(asset, revision);
	if (!inserted) {
		return;
	}
	try {
		if (!importService_.RequestLoadAsync(*assetDatabase_, asset)) {
			requested_.erase(entry);
		}
	} catch (...) {
		requested_.erase(entry);
		throw;
	}
}

void Engine::MeshGPUResourceManager::CompleteRequest(AssetID asset, uint64_t revision) {

	std::scoped_lock lock(mutex_);
	const auto active = requested_.find(asset);
	if (active == requested_.end() || active->second != revision) {
		return;
	}
	requested_.erase(active);
	const uint64_t latest = requestRevisions_.at(asset);
	if (latest != revision) {
		QueueLatestRequest(asset, latest);
	} else {
		layoutReloadRevisions_.erase(asset);
	}
}
