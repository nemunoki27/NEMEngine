#include "TextureUploadService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>

//============================================================================
//	TextureUploadService cacheMethods
//============================================================================
void Engine::TextureUploadService::InitCacheTracking(ID3D12Device* device) {

	cacheAdapter_ = DxUtils::FindDeviceAdapter(device);
}

void Engine::TextureUploadService::CollectUnusedTextures() {

	const auto now = std::chrono::steady_clock::now();
	if (!cacheAdapter_ || now - cacheCollectionTime_ < std::chrono::seconds(1)) return;
	cacheCollectionTime_ = now;
	DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
	if (FAILED(cacheAdapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)) ||
		memory.Budget == 0 || double(memory.CurrentUsage) <= double(memory.Budget) * 0.8) return;

	std::scoped_lock lock(mutex_);
	std::vector<std::string> unused;
	// 描画Cacheと転送中の要求が所有する画像は残す
	for (const auto& [key, texture] : readyTextures_) {

		const bool assetTexture = key.find(":texture:") != std::string::npos;
		const bool editorThumbnail = key.starts_with("editor:project:thumbnail:");
		if ((!assetTexture && !editorThumbnail) || queuedKeys_.contains(key) ||
			!texture.ownership || texture.ownership.use_count() != 1) continue;
		const auto access = textureAccessTimes_.find(key);
		if (access != textureAccessTimes_.end() && now - access->second >= std::chrono::seconds(5)) unused.push_back(key);
	}
	std::sort(unused.begin(), unused.end(), [&](const auto& left, const auto& right) {
		return textureAccessTimes_.at(left) < textureAccessTimes_.at(right);
	});
	uint64_t scheduledBytes = 0;
	// 未使用画像を古い順にFence回収へ渡す
	for (const auto& key : unused) {

		auto found = readyTextures_.find(key);
		srvDescriptor_->Retire(found->second.srvIndex, found->second.resource);
		scheduledBytes += found->second.allocationBytes;
		readyAllocationBytes_ -= found->second.allocationBytes;
		readyTextures_.erase(found);
		textureAccessTimes_.erase(key);
		if (double(memory.CurrentUsage) - double(scheduledBytes) <= double(memory.Budget) * 0.7) break;
	}
	if (scheduledBytes == 0) return;
	++contentRevision_;
	Logger::Output(LogType::Engine, "[TextureCache] 未使用画像の回収を予約しました bytes={} usage={} budget={}",
		scheduledBytes, memory.CurrentUsage, memory.Budget);
}

Engine::TextureCacheStatistics Engine::TextureUploadService::GetCacheStatistics() const {

	std::scoped_lock lock(mutex_);
	TextureCacheStatistics statistics;
	statistics.textureCount = readyTextures_.size();
	statistics.queuedCount = queuedKeys_.size();
	// 公開と待機列の変更時に集計した値を返す
	statistics.allocationBytes = readyAllocationBytes_;
	statistics.decodedBytes = decodedBytes_;
	statistics.uploadingCount = pendingGPUCount_.load(std::memory_order_relaxed);
	return statistics;
}
