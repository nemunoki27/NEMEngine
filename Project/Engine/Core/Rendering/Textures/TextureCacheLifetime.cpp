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

		if (key.find(":texture:") == std::string::npos || queuedKeys_.contains(key) ||
			!texture.ownership || texture.ownership.use_count() != 1) continue;
		const auto access = textureAccessTimes_.find(key);
		if (access != textureAccessTimes_.end() && now - access->second >= std::chrono::seconds(5)) unused.push_back(key);
	}
	std::sort(unused.begin(), unused.end(), [&](const auto& left, const auto& right) {
		return textureAccessTimes_.at(left) < textureAccessTimes_.at(right);
	});
	uint64_t releasedBytes = 0;
	ComPtr<ID3D12Device> device;
	// 未使用画像を古い順にFence回収へ渡す
	for (const auto& key : unused) {

		auto found = readyTextures_.find(key);
		if (!device && FAILED(found->second.resource->GetDevice(IID_PPV_ARGS(&device)))) break;
		const auto description = found->second.resource->GetDesc();
		releasedBytes += device->GetResourceAllocationInfo(0, 1, &description).SizeInBytes;
		srvDescriptor_->Retire(found->second.srvIndex, found->second.resource);
		readyTextures_.erase(found);
		textureAccessTimes_.erase(key);
		if (double(memory.CurrentUsage) - double(releasedBytes) <= double(memory.Budget) * 0.7) break;
	}
	if (releasedBytes == 0) return;
	++contentRevision_;
	Logger::Output(LogType::Engine, "[TextureCache] 未使用画像を回収しました bytes={} usage={} budget={}",
		releasedBytes, memory.CurrentUsage, memory.Budget);
}

Engine::TextureCacheStatistics Engine::TextureUploadService::GetCacheStatistics(ID3D12Device* device) const {

	std::scoped_lock lock(mutex_);
	TextureCacheStatistics statistics;
	statistics.textureCount = readyTextures_.size();
	statistics.queuedCount = queuedKeys_.size();
	// GPUの割当単位を含めて公開画像を集計する
	for (const auto& [key, texture] : readyTextures_) {

		const auto description = texture.resource->GetDesc();
		statistics.allocationBytes += device->GetResourceAllocationInfo(0, 1, &description).SizeInBytes;
	}
	for (const auto& request : pendingUploads_) statistics.decodedBytes += request.texture.image.GetPixelsSize();
	return statistics;
}
