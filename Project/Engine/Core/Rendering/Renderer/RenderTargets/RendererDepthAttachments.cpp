#include "RendererDepthAttachments.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>

Engine::DepthTexture2D* Engine::RendererDepthAttachments::Prepare(
	GraphicsCore& graphics, const MultiRenderTarget* target, uint32_t domain, uint32_t width, uint32_t height) {

	return Prepare(*graphics.GetDXObject().GetDxCommand(), graphics.GetDSVDescriptor(),
		graphics.GetSRVDescriptor(), target, domain, width, height);
}

Engine::DepthTexture2D* Engine::RendererDepthAttachments::Prepare(DxCommand& commands, DSVDescriptor& depths,
	SRVDescriptor& shaders, const MultiRenderTarget* target, uint32_t domain, uint32_t width, uint32_t height) {

	if (!target || domain >= 2 || width == 0 || height == 0) return nullptr;
	frameRegistration_.Bind(shaders.GetRetirementQueue().GetFrameResources(), this,
		[](void* owner, uint32_t count) { static_cast<RendererDepthAttachments*>(owner)->ReleaseInactiveFrames(count); });
	ReleaseInactiveFrames(GraphicsFrameState::GetActiveCount());
	auto& frame = frames_[GraphicsFrameState::GetCurrentIndex()];
	const uint64_t serial = GraphicsFrameState::GetFrameSerial();
	std::erase_if(frame, [serial](const auto& entry) {
		for (const auto& value : entry.second) {
			if (value.serial != UINT64_MAX && serial - value.serial <= 60) return false;
		}
		return true;
	});
	auto& attachment = frame[target][domain];
	if (!attachment.texture || attachment.texture->GetWidth() != width || attachment.texture->GetHeight() != height) {
		auto candidate = std::make_unique<DepthTexture2D>();
		DepthTextureCreateDesc desc{};
		desc.width = width;
		desc.height = height;
		desc.debugName = L"Renderer2DDepthStencil";
		candidate->Create(&depths, &shaders, desc);
		attachment.texture.swap(candidate);
		attachment.serial = UINT64_MAX;
	}
	attachment.texture->Transition(commands, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	if (attachment.serial != serial) {
		commands.GetCommandList()->ClearDepthStencilView(attachment.texture->GetDSVCPUHandle(),
			D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
		attachment.serial = serial;
	}
	return attachment.texture.get();
}

void Engine::RendererDepthAttachments::ReleaseInactiveFrames(uint32_t count) {

	for (uint32_t i = count; i < frames_.size(); ++i) frames_[i].clear();
}

void Engine::RendererDepthAttachments::Destroy() {

	for (auto& frame : frames_) frame.clear();
	frameRegistration_.Reset();
}

void Engine::RendererDepthAttachments::Swap(RendererDepthAttachments& other) noexcept {

	frames_.swap(other.frames_);
	frameRegistration_.Swap(other.frameRegistration_, this, &other);
}
