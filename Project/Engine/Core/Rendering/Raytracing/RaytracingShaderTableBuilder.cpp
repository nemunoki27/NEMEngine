#include "RaytracingPipelineBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <cstring>

namespace {
	UINT64 AlignUp(UINT64 value, UINT64 alignment) {

		return (value + alignment - 1) & ~(alignment - 1);
	}
}

bool Engine::RaytracingPipelineBuilder::BuildShaderTable(RaytracingPipelineState& state,
	ID3D12Device8* device,
	const std::vector<std::wstring>& rayGenerationExports,
	const std::vector<std::wstring>& missExports,
	const std::vector<std::wstring>& hitGroupExports,
	const std::vector<std::wstring>& callableExports) {

	state.rayGenerationCount_ = static_cast<uint32_t>(rayGenerationExports.size());
	state.missCount_ = static_cast<uint32_t>(missExports.size());
	state.hitGroupCount_ = static_cast<uint32_t>(hitGroupExports.size());
	state.callableCount_ = static_cast<uint32_t>(callableExports.size());
	state.rayGenerationTableOffset_ = 0;
	state.missTableOffset_ = AlignUp(state.rayGenerationCount_ * RaytracingPipelineState::kRecordStride, RaytracingPipelineState::kTableAlign);
	state.hitGroupTableOffset_ = AlignUp(
		state.missTableOffset_ + state.missCount_ * RaytracingPipelineState::kRecordStride, RaytracingPipelineState::kTableAlign);
	state.callableTableOffset_ = AlignUp(
		state.hitGroupTableOffset_ + state.hitGroupCount_ * RaytracingPipelineState::kRecordStride, RaytracingPipelineState::kTableAlign);
	state.shaderTableSize_ = static_cast<UINT>(AlignUp(
		state.callableTableOffset_ + state.callableCount_ * RaytracingPipelineState::kRecordStride, RaytracingPipelineState::kTableAlign));

	CD3DX12_RESOURCE_DESC bufferDesc =
		CD3DX12_RESOURCE_DESC::Buffer(state.shaderTableSize_);
	CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
	HRESULT result = device->CreateCommittedResource(&heapProperties,
		D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, IID_PPV_ARGS(&state.shaderTable_));
	if (FAILED(result) || !state.shaderTable_) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[レイトレーシングパイプライン] シェーダーテーブルの作成に失敗しました HRESULT=0x{:08X}",
			static_cast<uint32_t>(result));
		return false;
	}

	uint8_t* mapped = nullptr;
	result = state.shaderTable_->Map(0, nullptr,
		reinterpret_cast<void**>(&mapped));
	if (FAILED(result) || !mapped) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[レイトレーシングパイプライン] シェーダーテーブルのMapに失敗しました HRESULT=0x{:08X}",
			static_cast<uint32_t>(result));
		state.shaderTable_.Reset();
		return false;
	}
	std::memset(mapped, 0, state.shaderTableSize_);
	const auto writeTable = [&](const std::vector<std::wstring>& exports,
		UINT64 tableOffset) {

		for (size_t index = 0; index < exports.size(); ++index) {
			const void* identifier =
				state.stateProps_->GetShaderIdentifier(exports[index].c_str());
			if (!identifier) {
				return false;
			}
			std::memcpy(mapped + tableOffset + index * RaytracingPipelineState::kRecordStride,
				identifier, RaytracingPipelineState::kHandleSize);
		}
		return true;
	};
	const bool written =
		writeTable(rayGenerationExports, state.rayGenerationTableOffset_) &&
		writeTable(missExports, state.missTableOffset_) &&
		writeTable(hitGroupExports, state.hitGroupTableOffset_) &&
		writeTable(callableExports, state.callableTableOffset_);
	state.shaderTable_->Unmap(0, nullptr);
	if (!written) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[レイトレーシングパイプライン] シェーダー識別子が見つかりません");
		state.shaderTable_.Reset();
		return false;
	}
	return true;
}
