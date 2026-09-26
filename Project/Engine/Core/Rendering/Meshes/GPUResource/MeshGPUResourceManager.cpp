#include "MeshGPUResourceManager.h"

//============================================================================
//	include
//============================================================================
#include "MeshGPUBuilder.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>

// c++
#include <algorithm>
#include <cmath>
#include <span>

//============================================================================
//	MeshGPUResourceManager classMethods
//============================================================================

Engine::MeshGPUResourceManager::~MeshGPUResourceManager() {

	Finalize();
}

void Engine::MeshGPUResourceManager::Init(GraphicsCore& graphicsCore) {

	// すでに初期化されている場合は何もしない
	if (initialized_) {
		return;
	}

	device_ = graphicsCore.GetDXObject().GetDevice();
	srvDescriptor_ = &graphicsCore.GetSRVDescriptor();
	uploadService_ = &graphicsCore.GetBufferUploadService();

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
		gpuMeshes_.clear();
		requested_.clear();
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

	{
		std::scoped_lock lock(mutex_);
		if (gpuMeshes_.contains(meshAssetID) || requested_.contains(meshAssetID)) {
			return;
		}
	}

	// 実際にロード要求が通ったときだけ
	if (!importService_.RequestLoadAsync(assetDatabase, meshAssetID)) {
		return;
	}

	std::scoped_lock lock(mutex_);
	if (!gpuMeshes_.contains(meshAssetID)) {
		requested_.insert(meshAssetID);
	}
}

void Engine::MeshGPUResourceManager::RequestReload(AssetID meshAssetID) {

	if (!meshAssetID || !assetDatabase_) {
		return;
	}

	{
		std::scoped_lock lock(mutex_);
		auto it = gpuMeshes_.find(meshAssetID);
		// まだロードされていないメッシュは差し替える対象が無いので無視する
		if (it == gpuMeshes_.end()) {
			return;
		}

		// 旧GPU資源とDescriptorは描画完了まで回収窓口に残す
		ReleaseMeshResource(it->second);
		gpuMeshes_.erase(it);
		requested_.erase(meshAssetID);
		// 世代を進めて、TLAS等のキャッシュが古いジオメトリを使わないようにする
		++reloadGeneration_[meshAssetID];
		++resourceRevision_;
	}

	// インポートサービスは初回ロード後にidの記録を残さないため、同じ要求で再パースされる
	if (importService_.RequestLoadAsync(*assetDatabase_, meshAssetID)) {

		std::scoped_lock lock(mutex_);
		requested_.insert(meshAssetID);
	}
}

void Engine::MeshGPUResourceManager::ReleaseMeshResource(MeshGPUResource& mesh) {

	MeshGPUBuilder::Release(mesh);
}

void Engine::MeshGPUResourceManager::FlushUploads() {

	// 読み込み待ちのメッシュアセットのうち、GPUにアップロードされていないものをアップロードする
	std::vector<AssetID> pending{};
	{
		std::scoped_lock lock(mutex_);
		pending.reserve(requested_.size());
		for (const AssetID& id : requested_) {
			pending.emplace_back(id);
		}
	}

	for (const AssetID& id : pending) {

		ImportedMeshAsset imported{};
		if (!importService_.TakeImported(id, imported)) {
			continue;
		}

		{
			std::scoped_lock lock(mutex_);
			if (gpuMeshes_.contains(id)) {
				requested_.erase(id);
				continue;
			}
		}

		// GPUにアップロード
		UploadImported(imported);

		// アップロード完了したものは要求リストから削除
		{
			std::scoped_lock lock(mutex_);
			requested_.erase(id);
		}
	}
}

void Engine::MeshGPUResourceManager::WaitAll() {

	importService_.WaitAll();
	FlushUploads();
}

const Engine::MeshGPUResource* Engine::MeshGPUResourceManager::Find(AssetID meshAssetID) const {

	std::scoped_lock lock(mutex_);
	auto it = gpuMeshes_.find(meshAssetID);
	if (it == gpuMeshes_.end()) {
		return nullptr;
	}
	return &it->second;
}

void Engine::MeshGPUResourceManager::UploadImported(const ImportedMeshAsset& imported) {

	if (!uploadService_) {
		return;
	}

	MeshGPUResource mesh = MeshGPUBuilder::Create(imported, assetDatabase_, device_, uploadService_, srvDescriptor_);

	// GPUリソースを保存
	{
		std::scoped_lock lock(mutex_);
		// 現在のリロード世代を焼き込み、BLAS等のキャッシュが差し替えを検知できるようにする
		mesh.reloadGeneration = reloadGeneration_[imported.assetID];
		gpuMeshes_.emplace(imported.assetID, std::move(mesh));
		++resourceRevision_;
	}
}
