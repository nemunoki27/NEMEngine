#include "ParticleOpaqueDepthSnapshot.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>

using namespace Engine;

DepthTexture2D* ParticleOpaqueDepthSnapshot::Copy(GraphicsCore& graphics, DepthTexture2D& source) {

	return Copy(*graphics.GetDXObject().GetDxCommand(), graphics.GetDSVDescriptor(), graphics.GetSRVDescriptor(), source);
}

DepthTexture2D* ParticleOpaqueDepthSnapshot::Copy(DxCommand& commands, DSVDescriptor& depths,
	SRVDescriptor& shaders, DepthTexture2D& source) {

	if (!source.IsValid()) return nullptr;
	frameRegistration_.Bind(shaders.GetRetirementQueue().GetFrameResources(), this, [](void* owner, uint32_t count) {
		static_cast<ParticleOpaqueDepthSnapshot*>(owner)->ReleaseInactiveFrames(count);
	});
	ReleaseInactiveFrames(GraphicsFrameState::GetActiveCount());
	auto& frame = frames_[GraphicsFrameState::GetCurrentIndex()];
	const uint64_t serial = GraphicsFrameState::GetFrameSerial();
	if (frame.serial == serial && frame.source == source.GetResource()) return frame.texture.get();
	if (!frame.texture || frame.texture->GetWidth() != source.GetWidth() || frame.texture->GetHeight() != source.GetHeight() ||
		frame.texture->GetResourceFormat() != source.GetResourceFormat()) {
		auto candidate = std::make_unique<DepthTexture2D>();
		DepthTextureCreateDesc desc;
		desc.width = source.GetWidth();
		desc.height = source.GetHeight();
		desc.resourceFormat = source.GetResourceFormat();
		desc.dsvFormat = source.GetDSVFormat();
		desc.srvFormat = source.GetSRVFormat();
		desc.debugName = L"ParticleOpaqueDepth";
		candidate->Create(&depths, &shaders, desc);
		frame.texture.swap(candidate);
	}
	source.Transition(commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
	frame.texture->Transition(commands, D3D12_RESOURCE_STATE_COPY_DEST);
	commands.GetCommandList()->CopyResource(frame.texture->GetResource(), source.GetResource());
	frame.texture->Transition(commands, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	frame.source = source.GetResource();
	frame.serial = serial;
	return frame.texture.get();
}

void ParticleOpaqueDepthSnapshot::Swap(ParticleOpaqueDepthSnapshot& other) noexcept {

	frames_.swap(other.frames_);
	frameRegistration_.Swap(other.frameRegistration_, this, &other);
}

void ParticleOpaqueDepthSnapshot::Destroy() {

	for (auto& frame : frames_) frame = {};
	frameRegistration_.Reset();
}

void ParticleOpaqueDepthSnapshot::ReleaseInactiveFrames(uint32_t count) {

	for (uint32_t i = count; i < frames_.size(); ++i) frames_[i] = {};
}

DepthTexture2D* ParticleOpaqueDepthSnapshot::GetTexture() const {

	return frames_[GraphicsFrameState::GetCurrentIndex()].texture.get();
}

uint64_t ParticleOpaqueDepthSnapshot::GetFrameSerial() const {

	return frames_[GraphicsFrameState::GetCurrentIndex()].serial;
}

DepthTexture2D* ParticleOpaqueDepthSnapshot::GetLatestTexture() const {

	const Frame* latest = nullptr;
	for (const auto& frame : frames_) {
		if (frame.serial != UINT64_MAX && (!latest || latest->serial < frame.serial)) latest = &frame;
	}
	return latest ? latest->texture.get() : nullptr;
}
