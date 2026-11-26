#include "GPUFrameProfiler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>

// c++
#include <algorithm>

// directX
#include <d3dx12.h>

//============================================================================
//	ViewVertexCounter classMethods
//============================================================================
void Engine::ViewVertexCounter::AddIndirect(ID3D12GraphicsCommandList* commands, ID3D12Resource* arguments,
	uint64_t instanceCountOffset, uint32_t vertices) {

	if (incomplete_) return;
	const size_t page = weights_.size() / kCountsPerPage;
	if (pages_.size() == page) {

		ComPtr<ID3D12Device> device;
		ComPtr<ID3D12Resource> buffer;
		const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_READBACK);
		const auto desc = CD3DX12_RESOURCE_DESC::Buffer(kCountsPerPage * sizeof(uint32_t));
		if (FAILED(arguments->GetDevice(IID_PPV_ARGS(&device))) ||
			FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
				D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer)))) {
			incomplete_ = true;
			return;
		}
		pages_.emplace_back(std::move(buffer));
	}

	// 次の描画で引数が更新される前に可視インスタンス数だけ退避する
	commands->CopyBufferRegion(pages_[page].Get(), (weights_.size() % kCountsPerPage) * sizeof(uint32_t),
		arguments, instanceCountOffset, sizeof(uint32_t));
	weights_.emplace_back(vertices);
}

std::optional<uint64_t> Engine::ViewVertexCounter::Collect() const {

	if (!rendered_ || incomplete_) return std::nullopt;
	uint64_t result = vertices_;
	for (size_t first = 0; first < weights_.size(); first += kCountsPerPage) {

		const size_t count = (std::min)(kCountsPerPage, weights_.size() - first);
		const D3D12_RANGE range{ 0, count * sizeof(uint32_t) };
		void* data = nullptr;
		auto& buffer = pages_[first / kCountsPerPage];
		if (FAILED(buffer->Map(0, &range, &data))) return std::nullopt;
		const auto* instances = static_cast<const uint32_t*>(data);
		for (size_t index = 0; index < count; ++index) {
			result += uint64_t(instances[index]) * weights_[first + index];
		}
		const D3D12_RANGE written{ 0, 0 };
		buffer->Unmap(0, &written);
	}
	return result;
}

void Engine::ViewVertexCounter::Reset() {

	vertices_ = 0;
	rendered_ = false;
	incomplete_ = false;
	weights_.clear();
}

//============================================================================
//	GPUFrameProfiler classMethods
//============================================================================
Engine::GPUFrameProfiler& Engine::GPUFrameProfiler::GetInstance() {

	static GPUFrameProfiler instance;
	return instance;
}

bool Engine::GPUFrameProfiler::EnsureInitialized(ID3D12Device* device, ID3D12CommandQueue* commandQueue) {

	if (initialized_ && frameStates_[GraphicsFrameState::GetCurrentIndex()].queryHeap) {
		return true;
	}
	if (!device || !commandQueue) {
		return false;
	}

	std::array<FrameQueryState, kGraphicsFrameContextCount> candidates{};
	for (uint32_t index = 0; index < GraphicsFrameState::GetActiveCount(); ++index) {
		if (frameStates_[index].queryHeap) continue;
		auto& state = candidates[index];

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

	// 全slotの生成後に計測用資源を公開する
	for (uint32_t index = 0; index < GraphicsFrameState::GetActiveCount(); ++index) {
		if (candidates[index].queryHeap) frameStates_[index] = std::move(candidates[index]);
	}
	initialized_ = true;
	return true;
}

void Engine::GPUFrameProfiler::BeginFrame(ID3D12Device* device, ID3D12CommandQueue* commandQueue,
	GraphicsResourceRetirement& retirement) {

	retirement_ = &retirement;
	frameRegistration_.Bind(retirement.GetFrameResources(), this, [](void* owner, uint32_t count) {
		auto& profiler = *static_cast<GPUFrameProfiler*>(owner);
		for (uint32_t index = count; index < kGraphicsFrameContextCount; ++index) {
			profiler.frameStates_[index] = {};
		}
	});
	if (!FrameProfiler::GetInstance().IsEnabled()) { return; }

	if (!EnsureInitialized(device, commandQueue)) {
		return;
	}

	FrameQueryState& state =
		frameStates_[GraphicsFrameState::GetCurrentIndex()];
	// 同じframeの別Viewではtimestampを引き継ぐ
	const uint64_t serial = GraphicsFrameState::GetFrameSerial();
	if (state.frameSerial == serial) {
		state.active = true;
		return;
	}
	// 同じContextの前回結果はBeginFrameのFence待機後なので安全に読める
	if (state.hasResolved) {
		for (size_t index = 0; index < state.vertices.size(); ++index) {
			vertexCounts_[index] = state.vertices[index].Collect();
		}
	}
	for (auto& counter : state.vertices) counter.Reset();
	CollectResolved(state);
	state.frameSerial = serial;
	state.cpuFrameID = FrameProfiler::GetInstance().GetFrameID();
	state.incomplete = false;

	// このフレームの記録をリセットする
	state.nextTimestamp = 0;
	state.passes.clear();
	state.pendingPass = false;
	state.ignoredPassDepth = 0;
	state.active = true;
}

void Engine::GPUFrameProfiler::BeginPass(ID3D12GraphicsCommandList* commandList, const std::string& name) {

	FrameQueryState& state =
		frameStates_[GraphicsFrameState::GetCurrentIndex()];
	if (!state.active || !commandList) {
		return;
	}
	// 入れ子の区間を合計へ二重加算しない
	if (state.pendingPass) { ++state.ignoredPassDepth; return; }
	// begin/endの2つ分が残っていなければ計測しない
	if (kMaxTimestamps < state.nextTimestamp + 2) {
		state.incomplete = true;
		return;
	}

	state.pendingBegin = state.nextTimestamp++;
	commandList->EndQuery(state.queryHeap.Get(),
		D3D12_QUERY_TYPE_TIMESTAMP, state.pendingBegin);
	state.pendingName = name;
	state.pendingViewID = viewID_;
	state.pendingPass = true;
}

void Engine::GPUFrameProfiler::EndPass(ID3D12GraphicsCommandList* commandList) {

	FrameQueryState& state =
		frameStates_[GraphicsFrameState::GetCurrentIndex()];
	if (state.ignoredPassDepth) { --state.ignoredPassDepth; return; }
	if (!state.active || !commandList || !state.pendingPass) {
		return;
	}

	const uint32_t endIndex = state.nextTimestamp++;
	commandList->EndQuery(state.queryHeap.Get(),
		D3D12_QUERY_TYPE_TIMESTAMP, endIndex);
	state.passes.push_back({
		state.pendingName, state.pendingBegin, endIndex, state.pendingViewID
		});
	state.pendingPass = false;
}

void Engine::GPUFrameProfiler::RequestVertexCounts() {

	vertexRequestFrame_ = FrameProfiler::GetInstance().GetFrameID();
}

Engine::ViewVertexCounter* Engine::GPUFrameProfiler::GetVertexCounter(RenderViewKind kind) {

	auto& state = frameStates_[GraphicsFrameState::GetCurrentIndex()];
	if (!state.active || state.frameSerial != GraphicsFrameState::GetFrameSerial() || vertexRequestFrame_ == UINT64_MAX ||
		2 < FrameProfiler::GetInstance().GetFrameID() - vertexRequestFrame_) return nullptr;
	auto& counter = state.vertices[static_cast<size_t>(kind)];
	counter.rendered_ = true;
	return &counter;
}

std::optional<uint64_t> Engine::GPUFrameProfiler::GetVertexCount(RenderViewKind kind) const {

	return vertexCounts_[static_cast<size_t>(kind)];
}

void Engine::GPUFrameProfiler::SetViewID(std::string viewID) {

	viewID_ = std::move(viewID);
}

void Engine::GPUFrameProfiler::Resolve(ID3D12GraphicsCommandList* commandList) {

	FrameQueryState& state =
		frameStates_[GraphicsFrameState::GetCurrentIndex()];
	if (!initialized_ || !state.active || !commandList) {
		return;
	}
	state.active = false;
	state.incomplete |= state.pendingPass;

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
		if (state.hasResolved) {
			FrameProfiler::GetInstance().SetGPUFrame(state.cpuFrameID, {}, "missing");
			state.hasResolved = false;
		}
		return;
	}

	// 解決済み範囲だけをMapして読み出す
	const D3D12_RANGE readRange{
		0, static_cast<SIZE_T>(state.resolvedCount) * sizeof(uint64_t)
	};
	void* mapped = nullptr;
	if (FAILED(state.readbackBuffer->Map(0, &readRange, &mapped)) || !mapped) {
		FrameProfiler::GetInstance().SetGPUFrame(state.cpuFrameID, {}, "readback_failed");
		state.hasResolved = false;
		return;
	}

	const uint64_t* timestamps = static_cast<const uint64_t*>(mapped);
	std::vector<FrameProfiler::NamedTime> passTimes;
	passTimes.reserve(state.resolvedPasses.size());
	for (const PassRecord& pass : state.resolvedPasses) {

		const uint64_t begin = timestamps[pass.beginIndex];
		const uint64_t end = timestamps[pass.endIndex];
		// 逆転したタイムスタンプを正常な0msとして公開しない
		if (end < begin) { state.incomplete = true; continue; }
		const float milliseconds = (begin < end) ?
			static_cast<float>(end - begin) / static_cast<float>(frequency_) * 1000.0f : 0.0f;
		passTimes.push_back({ pass.name, milliseconds, state.cpuFrameID, pass.viewID });
	}

	const D3D12_RANGE writtenRange{ 0, 0 };
	state.readbackBuffer->Unmap(0, &writtenRange);

	FrameProfiler::GetInstance().SetGPUFrame(state.cpuFrameID, passTimes, state.incomplete ? "partial" : "complete");
	state.hasResolved = false;
}

void Engine::GPUFrameProfiler::Finalize() {

	// GPU完了までQueryと読戻し先を保持する
	for (FrameQueryState& state : frameStates_) {
		if (retirement_) {
			for (auto& counter : state.vertices) {
				for (auto& page : counter.pages_) retirement_->Retire(page);
			}
			retirement_->Retire(state.queryHeap);
			retirement_->Retire(state.readbackBuffer);
		}
		state = {};
	}
	frameRegistration_.Reset();
	retirement_ = nullptr;
	vertexRequestFrame_ = UINT64_MAX;
	vertexCounts_ = {};
	frequency_ = 0;
	initialized_ = false;
}

//============================================================================
//	ScopedGPUProfile classMethods
//============================================================================
Engine::ScopedGPUProfile::ScopedGPUProfile(ID3D12GraphicsCommandList* commandList, const std::string& name) : commandList_(commandList) {

	GPUFrameProfiler::GetInstance().BeginPass(commandList_, name);
}

Engine::ScopedGPUProfile::~ScopedGPUProfile() {

	GPUFrameProfiler::GetInstance().EndPass(commandList_);
}
