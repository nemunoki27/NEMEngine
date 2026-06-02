#include "GpuFrameProfiler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Time/FrameProfiler.h>

// directX
#include <d3dx12.h>

//============================================================================
//	GpuFrameProfiler classMethods
//============================================================================

Engine::GpuFrameProfiler& Engine::GpuFrameProfiler::GetInstance() {

	static GpuFrameProfiler instance;
	return instance;
}

bool Engine::GpuFrameProfiler::EnsureInitialized(ID3D12Device* device, ID3D12CommandQueue* commandQueue) {

	if (initialized_) {
		return true;
	}
	if (!device || !commandQueue) {
		return false;
	}

	// タイムスタンプクエリヒープを作成する
	D3D12_QUERY_HEAP_DESC heapDesc{};
	heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	heapDesc.Count = kMaxTimestamps;
	heapDesc.NodeMask = 0;
	if (FAILED(device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&queryHeap_)))) {
		return false;
	}

	// 解決結果を受け取るリードバックバッファを作成する(READBACKヒープはCOPY_DEST固定)
	const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_READBACK);
	const CD3DX12_RESOURCE_DESC bufferDesc =
		CD3DX12_RESOURCE_DESC::Buffer(static_cast<uint64_t>(kMaxTimestamps) * sizeof(uint64_t));
	if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readbackBuffer_)))) {
		return false;
	}

	// タイムスタンプの周波数(tick/sec)を取得する
	if (FAILED(commandQueue->GetTimestampFrequency(&frequency_)) || frequency_ == 0) {
		return false;
	}

	initialized_ = true;
	return true;
}

void Engine::GpuFrameProfiler::BeginFrame(ID3D12Device* device, ID3D12CommandQueue* commandQueue) {

	if (!EnsureInitialized(device, commandQueue)) {
		return;
	}

	// 前フレームの解決結果(GPU完了済み)を読み出してFrameProfilerへ反映する
	CollectResolved();

	// このフレームの記録をリセットする
	nextTimestamp_ = 0;
	passes_.clear();
	pendingPass_ = false;
	active_ = true;
}

void Engine::GpuFrameProfiler::BeginPass(ID3D12GraphicsCommandList* commandList, const std::string& name) {

	if (!active_ || !commandList || pendingPass_) {
		return;
	}
	// begin/endの2つ分が残っていなければ計測しない
	if (kMaxTimestamps < nextTimestamp_ + 2) {
		return;
	}

	pendingBegin_ = nextTimestamp_++;
	commandList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, pendingBegin_);
	pendingName_ = name;
	pendingPass_ = true;
}

void Engine::GpuFrameProfiler::EndPass(ID3D12GraphicsCommandList* commandList) {

	if (!active_ || !commandList || !pendingPass_) {
		return;
	}

	const uint32_t endIndex = nextTimestamp_++;
	commandList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, endIndex);
	passes_.push_back({ pendingName_, pendingBegin_, endIndex });
	pendingPass_ = false;
}

void Engine::GpuFrameProfiler::Resolve(ID3D12GraphicsCommandList* commandList) {

	if (!initialized_ || !active_ || !commandList) {
		return;
	}
	active_ = false;

	if (0 < nextTimestamp_) {

		commandList->ResolveQueryData(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
			0, nextTimestamp_, readbackBuffer_.Get(), 0);
	}

	// 次フレームの読み出し用に記録を退避する
	resolvedPasses_ = passes_;
	resolvedCount_ = nextTimestamp_;
	hasResolved_ = true;
}

void Engine::GpuFrameProfiler::CollectResolved() {

	if (!hasResolved_ || resolvedCount_ == 0 || frequency_ == 0) {
		return;
	}

	// 解決済み範囲だけをMapして読み出す
	const D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(resolvedCount_) * sizeof(uint64_t) };
	void* mapped = nullptr;
	if (FAILED(readbackBuffer_->Map(0, &readRange, &mapped)) || !mapped) {
		return;
	}

	const uint64_t* timestamps = static_cast<const uint64_t*>(mapped);
	std::vector<FrameProfiler::NamedTime> passTimes;
	passTimes.reserve(resolvedPasses_.size());
	for (const PassRecord& pass : resolvedPasses_) {

		const uint64_t begin = timestamps[pass.beginIndex];
		const uint64_t end = timestamps[pass.endIndex];
		const float milliseconds = (begin < end) ?
			static_cast<float>(end - begin) / static_cast<float>(frequency_) * 1000.0f : 0.0f;
		passTimes.push_back({ pass.name, milliseconds });
	}

	const D3D12_RANGE writtenRange{ 0, 0 };
	readbackBuffer_->Unmap(0, &writtenRange);

	FrameProfiler::GetInstance().SetGpuPassTimes(passTimes);
}

void Engine::GpuFrameProfiler::Finalize() {

	queryHeap_.Reset();
	readbackBuffer_.Reset();
	initialized_ = false;
	active_ = false;
	hasResolved_ = false;
}
