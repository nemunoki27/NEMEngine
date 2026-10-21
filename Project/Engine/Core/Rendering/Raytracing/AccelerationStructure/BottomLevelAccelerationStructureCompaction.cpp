#include "BottomLevelAccelerationStructure.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>
#include <cstring>
#include <stdexcept>

void Engine::BottomLevelAccelerationStructure::RecordCompactionSize(
	ID3D12GraphicsCommandList6* commandList, const GraphicsSubmissionTicket& submission) {

	// 小さな形状は追加のResourceとコピーを作らない
	if (originalBytes_ < 64 * 1024) return;
	compaction_ = std::make_unique<CompactionState>();
	auto& state = *compaction_;
	state.build = submission;
	state.size.Create(device_, sizeof(uint64_t), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	state.readback.Create(device_, sizeof(uint64_t), D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_HEAP_TYPE_READBACK);
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC info{};
	info.DestBuffer = state.size.GetGPUAddress();
	info.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
	const auto address = result_.GetGPUAddress();
	commandList->EmitRaytracingAccelerationStructurePostbuildInfo(&info, 1, &address);
	state.size.InsertUAVBarrier(commandList);
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition = {state.size.GetResource(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
	commandList->ResourceBarrier(1, &barrier);
	commandList->CopyBufferRegion(state.readback.GetResource(), 0, state.size.GetResource(), 0, sizeof(uint64_t));
}

bool Engine::BottomLevelAccelerationStructure::ProcessCompaction(GraphicsCore& graphicsCore, uint64_t& copyBudget) {

	if (!compaction_) return false;
	auto& state = *compaction_;
	const auto completed = graphicsCore.GetDXObject().GetCommandQueue()->GetCompletedFenceValue();
	if (completed == UINT64_MAX) throw std::runtime_error("BLAS圧縮中にDeviceが失われました");
	const auto& ticket = state.copy ? state.copy : state.build;
	if (!ticket || !ticket->GetFenceValue() || completed < ticket->GetFenceValue()) return false;
	if (state.copy) {
		// コピー完了後にだけアドレスを公開し、旧ASは描画Fenceまで保持する
		retirementQueue_->ReservePending(4);
		retirementQueue_->Retire(result_.TakeResource());
		result_ = std::move(state.candidate);
		buildDesc_.DestAccelerationStructureData = result_.GetGPUAddress();
		ReleaseCompaction();
		return true;
	}
	if (!state.compactedBytes) {
		void* mapped = nullptr;
		const D3D12_RANGE read{0, sizeof(uint64_t)};
		if (!DxDREDDiagnostics::CheckHRESULT(device_, state.readback.GetResource()->Map(0, &read, &mapped), "BLAS::Compaction/Map")) {
			throw std::runtime_error("BLAS圧縮サイズを読み戻せませんでした");
		}
		std::memcpy(&state.compactedBytes, mapped, sizeof(uint64_t));
		const D3D12_RANGE written{0, 0};
		state.readback.GetResource()->Unmap(0, &written);
		state.compactedBytes = AccelerationStructureBuffer::AlignASSize(state.compactedBytes);
		if (!state.compactedBytes || state.compactedBytes >= originalBytes_) {
			ReleaseCompaction();
			return false;
		}
	}
	if (state.compactedBytes > copyBudget) return false;
	// 追加領域の予算が取れたときだけ圧縮コピーを記録する
	state.candidate.Create(device_, state.compactedBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
	auto& command = *graphicsCore.GetDXObject().GetDxCommand();
	command.GetCommandList()->CopyRaytracingAccelerationStructure(state.candidate.GetGPUAddress(), result_.GetGPUAddress(),
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
	state.candidate.InsertUAVBarrier(command.GetCommandList());
	state.copy = command.GetSubmissionTicket();
	copyBudget -= state.candidate.GetResource()->GetDesc().Width;
	return false;
}

void Engine::BottomLevelAccelerationStructure::ReleaseCompaction() {

	if (!compaction_) return;
	retirementQueue_->ReservePending(3);
	for (auto* buffer : {&compaction_->size, &compaction_->readback, &compaction_->candidate}) {
		if (buffer->GetResource()) retirementQueue_->Retire(buffer->TakeResource());
	}
	compaction_.reset();
}
