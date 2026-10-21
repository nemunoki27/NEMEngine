#include "RaytracingBLASCache.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>

bool Engine::RaytracingBLASCache::ProcessCompactions(GraphicsCore& graphicsCore) {

	// 元のASと圧縮先が共存できる予算だけを使う
	uint64_t copyBudget = 0;
	bool pending = false;
	for (const auto& [key, blas] : blases_) pending |= blas.HasPendingCompaction();
	for (const auto& [key, entry] : staticInstanceBLASes_) {
		for (const auto& blas : entry.lodBLASes) pending |= blas.HasPendingCompaction();
	}
	if (pending && !adapter_) adapter_ = DxUtils::FindDeviceAdapter(graphicsCore.GetDXObject().GetDevice());
	DXGI_QUERY_VIDEO_MEMORY_INFO info{};
	if (pending && adapter_ && SUCCEEDED(adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
		const uint64_t limit = info.Budget - info.Budget / 10;
		if (info.CurrentUsage < limit) copyBudget = limit - info.CurrentUsage;
	}
	bool changed = false;
	originalBytes_ = 0;
	publishedBytes_ = 0;
	auto process = [&](BottomLevelAccelerationStructure& blas) {

		const uint64_t before = copyBudget;
		changed = blas.ProcessCompaction(graphicsCore, copyBudget) || changed;
		// 一度に記録する圧縮コピーは1件に絞る
		if (copyBudget != before) copyBudget = 0;
		originalBytes_ += blas.GetOriginalBytes();
		if (auto* resource = blas.GetResource()) publishedBytes_ += resource->GetDesc().Width;
	};
	for (auto& [key, blas] : blases_) process(blas);
	for (auto& [key, entry] : staticInstanceBLASes_) {
		for (auto& blas : entry.lodBLASes) process(blas);
	}
	return changed;
}
