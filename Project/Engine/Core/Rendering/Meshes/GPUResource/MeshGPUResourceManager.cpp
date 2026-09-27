#include "MeshGPUResourceManager.h"

//============================================================================
//	include
//============================================================================
#include "MeshGPUBuilder.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>

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

	// メッシュインポートサービスの初期化
	importService_.Init(4);
	initialized_ = true;
}

void Engine::MeshGPUResourceManager::Finalize() {

	if (!initialized_) {
		return;
	}

	importService_.Finalize();
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
		requested_.clear();
		requestRevisions_.clear();
	}
	device_ = nullptr;
	srvDescriptor_ = nullptr;
	uploadService_ = nullptr;
	assetDatabase_ = nullptr;
	initialized_ = false;
}

void Engine::MeshGPUResourceManager::BeginFrame(GraphicsCore& graphicsCore) {

	// 初期化されていない場合は初期化する
	if (!initialized_) {

		Init(graphicsCore);
	}
}

void Engine::MeshGPUResourceManager::RequestMesh(AssetDatabase& assetDatabase, AssetID meshAssetID) {

	assetDatabase_ = &assetDatabase;

	// 無効なIDは無視
	if (!meshAssetID) {
		return;
	}

	std::scoped_lock lock(mutex_);
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
}

void Engine::MeshGPUResourceManager::ReleaseMeshResource(MeshGPUResource& mesh) {

	MeshGPUBuilder::Release(mesh);
}

void Engine::MeshGPUResourceManager::FlushUploads() {

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

		// GPUにアップロード
		try {
			UploadImported(imported, revision);
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
		FlushUploads();
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
	return &it->second;
}

void Engine::MeshGPUResourceManager::UploadImported(const ImportedMeshAsset& imported, uint64_t revision) {

	if (!uploadService_) {
		return;
	}
	{
		std::scoped_lock lock(mutex_);
		// 古い読込結果のGPU生成を省く
		if (requestRevisions_.at(imported.assetID) != revision) {
			return;
		}
	}

	MeshGPUResource mesh{};
	try {
		mesh = MeshGPUBuilder::Create(imported, assetDatabase_, device_, uploadService_, srvDescriptor_);
	}
	catch (const std::exception& exception) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"MeshのGPU資源作成に失敗しました asset={} 内容={}",
			ToString(imported.assetID), exception.what());
		return;
	}
	catch (...) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"MeshのGPU資源作成に失敗しました asset={} 内容=不明な例外",
			ToString(imported.assetID));
		return;
	}
	if (!mesh.IsValid()) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"MeshのGPU資源作成に失敗しました asset={}", ToString(imported.assetID));
		ReleaseMeshResource(mesh);
		return;
	}

	MeshGPUResource old{};
	bool retireOld = false;
	// 成功した候補を公開してから旧GPU資源を回収へ渡す
	{
		std::scoped_lock lock(mutex_);
		// GPU生成中に新しい要求を受けた候補も公開しない
		if (requestRevisions_.at(imported.assetID) != revision) {
			return;
		}
		auto it = gpuMeshes_.find(imported.assetID);
		auto& generation = reloadGeneration_[imported.assetID];
		if (generation == UINT32_MAX) {
			throw std::overflow_error("Meshの公開世代が上限に達しました");
		}
		mesh.reloadGeneration = generation + 1;
		if (it == gpuMeshes_.end()) {
			gpuMeshes_.emplace(imported.assetID, std::move(mesh));
		} else {
			old = std::move(it->second);
			it->second = std::move(mesh);
			retireOld = true;
		}
		++generation;
		++resourceRevision_;
	}
	if (retireOld) {
		ReleaseMeshResource(old);
	}
	// GPU公開に成功した世代だけ編集layoutの解析を失効させる
	MeshSubMeshAuthoring::InvalidateCachedLayout(imported.assetID);
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
	}
}
