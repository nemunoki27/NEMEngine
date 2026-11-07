#include "BackendDrawCommon.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Renderer/Lighting/HeightFogConstants.h>

void Engine::BackendDrawCommon::BindTransparentFog(const RenderDrawContext& context,
	const PipelineState& pipeline, const RenderItem& item, FrameConstantBufferAllocator& allocator,
	ID3D12GraphicsCommandList* commands) {

	const auto* drawBinding = pipeline.FindBindingByName("FogDrawConstants", ShaderBindingKind::CBV);
	if (!drawBinding) return;
	struct DrawConstants {
		uint32_t enabled = 0;
		uint32_t blendMode = 0;
		uint32_t padding[2]{};
	};
	const auto* fog = context.bufferRegistry ? context.bufferRegistry->Find("HeightFogConstants") : nullptr;
	DrawConstants draw{};
	draw.enabled = fog && fog->gpuAddress && item.cameraDomain == RenderCameraDomain::Perspective &&
		item.renderPhase == RenderPhase::Transparent && context.passKind == MaterialPassKind::Transparent;
	draw.blendMode = static_cast<uint32_t>(item.blendMode);
	auto& platform = context.graphicsCore->GetDXObject();
	const auto allocation = allocator.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), draw);
	RootBindingCommand::SetGraphicsCBV(commands, drawBinding, allocation.gpuAddress);
	if (const auto* binding = pipeline.FindBindingByName("HeightFogConstants", ShaderBindingKind::CBV)) {
		const auto address = fog && fog->gpuAddress ? fog->gpuAddress :
			allocator.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), HeightFogConstants{}).gpuAddress;
		RootBindingCommand::SetGraphicsCBV(commands, binding, address);
	}
}
