#include "BufferUploadService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace {

	constexpr size_t kLargePageBytes = 8 * 1024 * 1024;
	constexpr size_t kLargePageCount = 8;
	constexpr size_t kFrameUploadBytes = 32 * 1024 * 1024;
}

Engine::BufferUploadTicket Engine::BufferUploadService::BeginOwnedUploads(std::shared_ptr<const void> source) {

	if (!source || activeUpload_) throw std::logic_error("所有付き転送の開始状態が不正です");
	activeUpload_ = std::make_shared<BufferUploadCompletion>();
	activeUpload_->source = std::move(source);
	return activeUpload_;
}

void Engine::BufferUploadService::FinishOwnedUploads() {

	if (!activeUpload_) throw std::logic_error("所有付き転送が開始されていません");
	SubmitBatch();
	if (activeUpload_->pending == 0) activeUpload_->source.reset();
	activeUpload_.reset();
}

void Engine::BufferUploadService::CancelOwnedUploads(const BufferUploadTicket& ticket) {

	if (!ticket) return;
	ticket->canceled = true;
	if (activeUpload_ == ticket) {
		// 記録済みの小容量転送も所有を保って提出する
		FinishOwnedUploads();
	}
}

bool Engine::BufferUploadService::IsComplete(const BufferUploadTicket& ticket) const {

	return !ticket->canceled && ticket->pending == 0 && fence_->GetCompletedValue() != UINT64_MAX &&
		fence_->GetCompletedValue() >= ticket->fence;
}

void Engine::BufferUploadService::PumpOwnedUploads(bool unlimited) {

	if (!device_ || !fence_) return;
	const auto now = std::chrono::steady_clock::now();
	const uint64_t completed = fence_->GetCompletedValue();
	if (completed == UINT64_MAX) throw std::runtime_error("Buffer転送中にDeviceが失われました");
	if (largeUploadFrame_ != GraphicsFrameState::GetFrameSerial()) {
		largeUploadFrame_ = GraphicsFrameState::GetFrameSerial();
		largeUploadedThisFrame_ = 0;
	}
	// GPU完了済みの古い大容量ページを残し続けない
	if (ownedUploads_.empty()) {
		std::erase_if(largePages_, [&](const auto& page) {
			if (page.fence > completed || now - page.lastUsed < std::chrono::seconds(5)) return false;
			statistics_.largePooledBytes -= kLargePageBytes;
			return true;
		});
		return;
	}
	// 別の所有元が小容量転送を記録中なら後で処理する
	if (activeUpload_) return;
	const bool existingBatch = batchOpened_;
	std::array<size_t, kLargePageCount> used{};
	size_t usedCount = 0;
	std::vector<BufferUploadTicket> tickets;
	while (!ownedUploads_.empty() && usedCount < kLargePageCount &&
		(unlimited || largeUploadedThisFrame_ < kFrameUploadBytes)) {
		auto& upload = ownedUploads_.front();
		if (upload.ticket->canceled) {
			statistics_.pendingBytes -= upload.source.size_bytes() - upload.offset;
			if (--upload.ticket->pending == 0) upload.ticket->source.reset();
			ownedUploads_.pop_front();
			continue;
		}
		// frameスロットを使い回すためのCPU待機を追加しない
		if (!batchOpened_ && contexts_[contextIndex_].lastFenceValue > completed) break;
		size_t pageIndex = 0;
		for (; pageIndex < largePages_.size(); ++pageIndex) {
			if (largePages_[pageIndex].fence <= completed &&
				std::find(used.begin(), used.begin() + usedCount, pageIndex) == used.begin() + usedCount) break;
		}
		if (pageIndex == largePages_.size()) {
			if (largePages_.size() >= kLargePageCount) break;
			LargeUploadPage page;
			page.buffer.Create(*retirement_, device_, kLargePageBytes);
			largePages_.push_back(std::move(page));
			statistics_.largePooledBytes += kLargePageBytes;
			++statistics_.stagingCreationCount;
		} else ++statistics_.stagingReuseCount;
		EnsureBatchOpened();
		auto& page = largePages_[pageIndex];
		const size_t budget = unlimited ? kLargePageBytes : kFrameUploadBytes - largeUploadedThisFrame_;
		const size_t bytes = (std::min)({kLargePageBytes, upload.source.size_bytes() - upload.offset, budget});
		page.buffer.Write(upload.source.data() + upload.offset, bytes);
		page.lastUsed = now;
		// 初回と最終回だけ転送先の状態を切り替える
		const auto transition = [&](D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
			if (before == after) return;
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition = {upload.destination.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
			currentContext_->commandList->ResourceBarrier(1, &barrier);
		};
		if (upload.offset == 0) transition(upload.before, D3D12_RESOURCE_STATE_COPY_DEST);
		currentContext_->retainedResources.emplace_back(upload.destination);
		currentContext_->retainedResources.emplace_back(page.buffer.GetResource());
		currentContext_->commandList->CopyBufferRegion(upload.destination.Get(), upload.destinationOffset + upload.offset,
			page.buffer.GetResource(), 0, bytes);
		upload.offset += bytes;
		largeUploadedThisFrame_ += bytes;
		statistics_.uploadedBytes += bytes;
		statistics_.pendingBytes -= bytes;
		++statistics_.uploadCount;
		hasCommands_ = true;
		used[usedCount++] = pageIndex;
		tickets.push_back(upload.ticket);
		if (upload.offset == upload.source.size_bytes()) {
			transition(D3D12_RESOURCE_STATE_COPY_DEST, upload.after);
			if (--upload.ticket->pending == 0) upload.ticket->source.reset();
			ownedUploads_.pop_front();
		}
	}
	if (usedCount) {
		const uint64_t submitted = SubmitBatch(existingBatch);
		for (size_t i = 0; i < usedCount; ++i) largePages_[used[i]].fence = submitted;
		for (const auto& ticket : tickets) ticket->fence = submitted;
	}
}
