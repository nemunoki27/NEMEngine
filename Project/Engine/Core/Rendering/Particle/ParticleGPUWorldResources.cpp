#include "ParticleGPUWorldResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>

// c++
#include <array>
#include <stdexcept>

using namespace Engine;

namespace {

	// 空配列にも参照可能なSRVを用意する
	template <typename T>
	void UploadValues(StructuredInstanceBuffer<T>& buffer, const std::vector<T>& values) {

		const std::array<T, 1> empty{};
		buffer.Upload(values.empty() ? std::span<const T>(empty) : std::span<const T>(values));
	}
}

ParticleGPUWorldResources::~ParticleGPUWorldResources() {

	if (descriptors_ && emptyDepth_ != UINT32_MAX) descriptors_->Retire(emptyDepth_, {});
}

void ParticleGPUWorldResources::Init(ID3D12Device* device, SRVDescriptor& descriptors, GraphicsResourceRetirement& retirement) {

	if (device_ && (device_ != device || descriptors_ != &descriptors || retirement_ != &retirement))
		throw std::logic_error("粒子のWorld入力のDeviceが一致しません");
	device_ = device;
	descriptors_ = &descriptors;
	retirement_ = &retirement;
	forces_.Init(device, &descriptors);
	vectorFields_.Init(device, &descriptors);
	vectorSamples_.Init(device, &descriptors);
	splineSamples_.Init(device, &descriptors);
	splineRanges_.Init(device, &descriptors);
	colliders_.Init(device, &descriptors);
	collisionNodes_.Init(device, &descriptors);
	heightmaps_.Init(device, &descriptors);
	heightSamples_.Init(device, &descriptors);
	if (emptyDepth_ == UINT32_MAX) {
		D3D12_SHADER_RESOURCE_VIEW_DESC description{};
		description.Format = DXGI_FORMAT_R32_FLOAT;
		description.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		description.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		description.Texture2D.MipLevels = 1;
		descriptors.CreateSRV(emptyDepth_, nullptr, description);
	}
	frameRegistration_.Bind(retirement.GetFrameResources(), this, [](void* value, uint32_t count) {
		auto& resources = *static_cast<ParticleGPUWorldResources*>(value);
		for (uint32_t i = count; i < kGraphicsFrameContextCount; ++i) resources.owners_[i] = {};
	});
}

void ParticleGPUWorldResources::Retain(std::shared_ptr<const void> owner) {

	auto& frame = owners_[GraphicsFrameState::GetCurrentIndex()];
	const auto serial = GraphicsFrameState::GetFrameSerial();
	if (frame.serial != serial) { frame.values.clear(); frame.serial = serial; }
	if (owner) frame.values.push_back(std::move(owner));
}

void ParticleGPUWorldResources::Upload(std::shared_ptr<const ParticleGPUWorldSnapshot> snapshot) {

	if (!device_) throw std::logic_error("粒子のWorld入力が初期化されていません");
	snapshot_ = snapshot ? std::move(snapshot) : std::make_shared<const ParticleGPUWorldSnapshot>();
	UploadValues(forces_, snapshot_->forces);
	UploadValues(vectorFields_, snapshot_->vectorFields);
	UploadValues(vectorSamples_, snapshot_->vectorSamples);
	UploadValues(splineSamples_, snapshot_->splineSamples);
	UploadValues(splineRanges_, snapshot_->splineRanges);
	UploadValues(colliders_, snapshot_->colliders);
	UploadValues(collisionNodes_, snapshot_->collisionNodes);
	UploadValues(heightmaps_, snapshot_->heightmaps);
	UploadValues(heightSamples_, snapshot_->heightSamples);
	Retain(snapshot_);
}

void ParticleGPUWorldResources::Bind(ID3D12GraphicsCommandList* commands, const PipelineState& pipeline,
	const ParticleGPUDepthInput& depth) {

	if (!pipeline.FindBindingByName("ParticleGPUWorldConstants", ShaderBindingKind::CBV)) return;
	if (!snapshot_) Upload({});
	auto constants = snapshot_->constants;
	const bool ready = snapshot_->depth.requested && depth.srv.ptr && depth.width >= 2 && depth.height >= 2;
	constants.depthParameters.x = static_cast<float>(depth.width);
	constants.depthParameters.y = static_cast<float>(depth.height);
	constants.depthParameters.w = ready ? 1.0f : 0.0f;
	constants.depthViewProjection = depth.viewProjection;
	constants.depthInverseViewProjection = depth.inverseViewProjection;
	constants_.BeginFrame();
	const auto allocation = constants_.AllocateAndUpload(*retirement_, device_, constants);
	RootBindingCommand::SetComputeCBV(commands, pipeline.FindBindingByName("ParticleGPUWorldConstants", ShaderBindingKind::CBV), allocation.gpuAddress);
	const auto bind = [&](const char* name, const auto& buffer) {
		if (const auto* binding = pipeline.FindBindingByName(name, ShaderBindingKind::SRV))
			RootBindingCommand::SetComputeSRV(commands, binding, buffer.GetGPUAddress(), buffer.GetGPUHandle());
	};
	bind("gGPUForceFields", forces_);
	bind("gGPUVectorFields", vectorFields_);
	bind("gGPUVectorSamples", vectorSamples_);
	bind("gGPUSplineSamples", splineSamples_);
	bind("gGPUSplineRanges", splineRanges_);
	bind("gGPUColliders", colliders_);
	bind("gGPUCollisionNodes", collisionNodes_);
	bind("gGPUHeightmaps", heightmaps_);
	bind("gGPUHeightSamples", heightSamples_);
	if (const auto* binding = pipeline.FindBindingByName("gGPUCollisionDepth", ShaderBindingKind::SRV))
		RootBindingCommand::SetComputeSRV(commands, binding, 0, ready ? depth.srv : descriptors_->GetGPUHandle(emptyDepth_));
	if (ready) Retain(depth.owner);
}
