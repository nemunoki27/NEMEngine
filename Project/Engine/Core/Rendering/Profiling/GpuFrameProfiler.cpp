#include "GPUFrameProfiler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Time/FrameProfiler.h>

// directX
#include <d3dx12.h>

//============================================================================
//	GPUFrameProfiler classMethods
//============================================================================
Engine::GPUFrameProfiler& Engine::GPUFrameProfiler::GetInstance() {

	static GPUFrameProfiler instance;
	return instance;
}

bool Engine::GPUFrameProfiler::EnsureInitialized(ID3D12Device* device, ID3D12CommandQueue* commandQueue) {

	if (initialized_) {
		return true;
	}
	if (!device || !commandQueue) {
		return false;
	}

	for (FrameQueryState& state : frameStates_) {

		D3D12_QUERY_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		heapDesc.Count = kMaxTimestamps;
		heapDesc.NodeMask = 0;
		if (FAILED(device->CreateQueryHeap(
			&heapDesc, IID_PPV_ARGS(&state.queryHeap)))) {
			return false;
		}

		// READBACKもフレーム別に分離して未完了フレームの結果を上書きしない
		const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_READBACK);
		const CD3DX12_RESOURCE_DESC bufferDesc =
			CD3DX12_RESOURCE_DESC::Buffer(
				static_cast<uint64_t>(kMaxTimestamps) * sizeof(uint64_t));
		if (FAILED(device->CreateCommittedResource(&heapProps,
			D3D12_HEAP_FLAG_NONE, &bufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
			IID_PPV_ARGS(&state.readbackBuffer)))) {
			return false;
		}
	}

	// タイムスタンプの周波数(tick/sec)を取得する
	if (FAILED(commandQueue->GetTimestampFrequency(&frequency_)) || frequency_ == 0) {
		return false;
	}

	initialized_ = true;
	return true;
}

void Engine::GPUFrameProfiler::BeginFrame(ID3D12Device* device, ID3D12CommandQueue* commandQueue) {

	if (!EnsureInitialized(device, commandQueue)) {
		return;
	}

	FrameQueryState& state =
		frameStates_[GraphicsFrameState::GetCurrentIndex()];
	// 同じContextの前回結果はBeginFrameのFence待機後なので安全に読める
	CollectResolved(state);

	// このフレームの記録をリセットする
	state.nextTimestamp = 0;
	state.passes.clear();
	state.pendingPass = false;
	state.active = true;
}

void Engine::GPUFrameProfiler::BeginPass(ID3D12GraphicsCommandList* commandList, const std::string& name) {

	FrameQueryState& state =
		frameStates_[GraphicsFrameState::GetCurrentIndex()];
	if (!state.active || !commandList || state.pendingPass) {
		return;
	}
	// begin/endの2つ分が残っていなければ計測しない
	if (kMaxTimestamps < state.nextTimestamp + 2) {
		return;
	}

	state.pendingBegin = state.nextTimestamp++;
	commandList->EndQuery(state.queryHeap.Get(),
		D3D12_QUERY_TYPE_TIMESTAMP, state.pendingBegin);
	state.pendingName = name;
	state.pendingPass = true;
}

void Engine::GPUFrameProfiler::EndPass(ID3D12GraphicsCommandList* commandList) {

	FrameQueryState& state =
		frameStates_[GraphicsFrameState::GetCurrentIndex()];
	if (!state.active || !commandList || !state.pendingPass) {
		return;
	}

	const uint32_t endIndex = state.nextTimestamp++;
	commandList->EndQuery(state.queryHeap.Get(),
		D3D12_QUERY_TYPE_TIMESTAMP, endIndex);
	state.passes.push_back({
		state.pendingName, state.pendingBegin, endIndex
		});
	state.pendingPass = false;
}

void Engine::GPUFrameProfiler::Resolve(ID3D12GraphicsCommandList* commandList) {

	FrameQueryState& state =
		frameStates_[GraphicsFrameState::GetCurrentIndex()];
	if (!initialized_ || !state.active || !commandList) {
		return;
	}
	state.active = false;

	if (0 < state.nextTimestamp) {

		commandList->ResolveQueryData(state.queryHeap.Get(),
			D3D12_QUERY_TYPE_TIMESTAMP, 0, state.nextTimestamp,
			state.readbackBuffer.Get(), 0);
	}

	// 次フレームの読み出し用に記録を退避する
	state.resolvedPasses = state.passes;
	state.resolvedCount = state.nextTimestamp;
	state.hasResolved = true;
}

void Engine::GPUFrameProfiler::CollectResolved(FrameQueryState& state) {

	if (!state.hasResolved || state.resolvedCount == 0 || frequency_ == 0) {
		return;
	}

	// 解決済み範囲だけをMapして読み出す
	const D3D12_RANGE readRange{
		0, static_cast<SIZE_T>(state.resolvedCount) * sizeof(uint64_t)
	};
	void* mapped = nullptr;
	if (FAILED(state.readbackBuffer->Map(0, &readRange, &mapped)) || !mapped) {
		return;
	}

	const uint64_t* timestamps = static_cast<const uint64_t*>(mapped);
	std::vector<FrameProfiler::NamedTime> passTimes;
	passTimes.reserve(state.resolvedPasses.size());
	for (const PassRecord& pass : state.resolvedPasses) {

		const uint64_t begin = timestamps[pass.beginIndex];
		const uint64_t end = timestamps[pass.endIndex];
		const float milliseconds = (begin < end) ?
			static_cast<float>(end - begin) / static_cast<float>(frequency_) * 1000.0f : 0.0f;
		passTimes.push_back({ pass.name, milliseconds });
	}

	const D3D12_RANGE writtenRange{ 0, 0 };
	state.readbackBuffer->Unmap(0, &writtenRange);

	FrameProfiler::GetInstance().SetGPUPassTimes(passTimes);
	state.hasResolved = false;
}

void Engine::GPUFrameProfiler::Finalize() {

	for (FrameQueryState& state : frameStates_) {
		state = {};
	}
	initialized_ = false;
}
