#include "RaytracingPipelineState.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/Pipelines/ShaderSourcePathResolver.h>
#include <Engine/Core/Rendering/Shaders/ShaderCook.h>

// c++
#include <algorithm>
#include <atomic>
#include <cstring>

//============================================================================
//	RaytracingPipelineState classMethods
//============================================================================
uint64_t Engine::RaytracingPipelineState::NextUniqueID() {

	static std::atomic<uint64_t> counter{ 0 };
	return ++counter;
}

D3D12_DISPATCH_RAYS_DESC Engine::RaytracingPipelineState::BuildDispatchDesc(
	uint32_t width, uint32_t height, uint32_t depth,
	uint32_t rayGenerationIndex) const {

	D3D12_DISPATCH_RAYS_DESC desc{};
	if (!shaderTable_ || rayGenerationIndex >= rayGenerationCount_) {
		return desc;
	}
	const D3D12_GPU_VIRTUAL_ADDRESS baseAddress =
		shaderTable_->GetGPUVirtualAddress();
	desc.RayGenerationShaderRecord.StartAddress = baseAddress +
		rayGenerationTableOffset_ + rayGenerationIndex * kRecordStride;
	desc.RayGenerationShaderRecord.SizeInBytes = kRecordStride;
	if (missCount_ > 0) {
		desc.MissShaderTable.StartAddress = baseAddress + missTableOffset_;
		desc.MissShaderTable.SizeInBytes = missCount_ * kRecordStride;
		desc.MissShaderTable.StrideInBytes = kRecordStride;
	}
	if (hitGroupCount_ > 0) {
		desc.HitGroupTable.StartAddress = baseAddress + hitGroupTableOffset_;
		desc.HitGroupTable.SizeInBytes = hitGroupCount_ * kRecordStride;
		desc.HitGroupTable.StrideInBytes = kRecordStride;
	}
	if (callableCount_ > 0) {
		desc.CallableShaderTable.StartAddress = baseAddress + callableTableOffset_;
		desc.CallableShaderTable.SizeInBytes = callableCount_ * kRecordStride;
		desc.CallableShaderTable.StrideInBytes = kRecordStride;
	}
	desc.Width = width;
	desc.Height = height;
	desc.Depth = depth;
	return desc;
}

const Engine::RootBindingLocation*
Engine::RaytracingPipelineState::FindBindingByName(
	std::string_view name, ShaderBindingKind kind) const {

	const auto found = std::find_if(bindings_.begin(), bindings_.end(),
		[&](const RootBindingLocation& binding) {
			return binding.kind == kind && binding.name == name;
		});
	return found != bindings_.end() ? &*found : nullptr;
}

Engine::RaytracingPipelineState::~RaytracingPipelineState() {

	if (retirement_) {
		RetireGPUObjects(*retirement_);
	}
}

void Engine::RaytracingPipelineState::SetRetirementQueue(GraphicsResourceRetirement& retirement) {

	Assert::Call(!retirement_ || retirement_ == &retirement, "使用中のPipelineの回収窓口は変更できません");
	retirement_ = &retirement;
}

void Engine::RaytracingPipelineState::RetireGPUObjects(GraphicsResourceRetirement& retirement) const {

	retirement.Retire(stateObject_);
	retirement.Retire(globalRootSignature_);
	retirement.Retire(shaderTable_);
}
