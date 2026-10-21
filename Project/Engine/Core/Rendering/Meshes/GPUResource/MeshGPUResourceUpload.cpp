#include "MeshGPUResourceManager.h"
#include "MeshGPUBuilder.h"
#include "MeshUploadPreparation.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <stdexcept>

bool Engine::MeshGPUResourceManager::UploadImported(ImportedMeshAsset imported, uint64_t revision) {

	if (!uploadService_) return false;
	const AssetID asset = imported.assetID;
	{
		std::scoped_lock lock(mutex_);
		if (requestRevisions_.at(asset) != revision) return false;
	}
	// workerの配列を移動し、分割転送が読み終えるまで共有する
	auto owner = std::make_shared<ImportedMeshAsset>(std::move(imported));
	if (!owner->uploadData) owner->uploadData = std::make_shared<MeshUploadData>(MeshUploadPreparation::Build(*owner));
	const auto ticket = uploadService_->BeginOwnedUploads(owner);
	MeshGPUResource mesh;
	try {
		mesh = MeshGPUBuilder::Create(*owner, assetDatabase_, device_, uploadService_, srvDescriptor_);
		uploadService_->FinishOwnedUploads();
		if (!mesh.IsValid()) {
			uploadService_->CancelOwnedUploads(ticket);
			ReleaseMeshResource(mesh);
			return false;
		}
		pendingMeshes_.push_back({asset, revision, std::move(mesh), ticket});
	} catch (const std::exception& exception) {
		uploadService_->CancelOwnedUploads(ticket);
		Logger::Output(LogType::Engine, spdlog::level::err,
			"MeshのGPU資源作成に失敗しました asset={} 内容={}", ToString(asset), exception.what());
		return false;
	} catch (...) {
		uploadService_->CancelOwnedUploads(ticket);
		throw;
	}
	return true;
}

void Engine::MeshGPUResourceManager::PublishReadyMeshes() {

	for (auto it = pendingMeshes_.begin(); it != pendingMeshes_.end();) {
		bool stale = false;
		{
			std::scoped_lock lock(mutex_);
			stale = requestRevisions_.at(it->asset) != it->revision;
		}
		if (stale) uploadService_->CancelOwnedUploads(it->ticket);
		else if (!uploadService_->IsComplete(it->ticket)) { ++it; continue; }
		// GPU完了と要求世代を確定してから旧Meshを差し替える
		const AssetID asset = it->asset;
		const uint64_t revision = it->revision;
		if (!stale) PublishMesh(std::move(it->mesh), asset, revision);
		it = pendingMeshes_.erase(it);
		CompleteRequest(asset, revision);
	}
}

void Engine::MeshGPUResourceManager::PublishMesh(MeshGPUResource mesh, AssetID asset, uint64_t revision) {

	MeshGPUResource old{};
	mesh.ownership = std::make_shared<const uint8_t>(0);
	bool retireOld = false;
	bool reloadLayout = false;
	// 成功した候補を公開してから旧GPU資源を回収へ渡す
	{
		std::scoped_lock lock(mutex_);
		// GPU生成中に新しい要求を受けた候補も公開しない
		if (requestRevisions_.at(asset) != revision) {
			return;
		}
		auto it = gpuMeshes_.find(asset);
		auto& generation = reloadGeneration_[asset];
		if (generation == UINT32_MAX) {
			throw std::overflow_error("Meshの公開世代が上限に達しました");
		}
		mesh.reloadGeneration = generation + 1;
		meshAccessTimes_[asset] = std::chrono::steady_clock::now();
		if (it == gpuMeshes_.end()) {
			gpuMeshes_.emplace(asset, std::move(mesh));
		} else {
			old = std::move(it->second);
			it->second = std::move(mesh);
			retireOld = true;
		}
		++generation;
		++resourceRevision_;
		const auto layoutReload = layoutReloadRevisions_.find(asset);
		reloadLayout = layoutReload != layoutReloadRevisions_.end() && layoutReload->second == revision;
	}
	if (retireOld) {
		ReleaseMeshResource(old);
	}
	// 未使用Meshの再転送では編集layoutを解析し直さない
	if (reloadLayout) MeshSubMeshAuthoring::InvalidateCachedLayout(asset);
}

