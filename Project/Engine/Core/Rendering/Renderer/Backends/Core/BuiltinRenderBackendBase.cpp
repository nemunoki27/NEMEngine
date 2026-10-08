#include "BuiltinRenderBackendBase.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

//============================================================================
//	BuiltinRenderBackendBase classMethods
//============================================================================
bool Engine::BuiltinRenderBackendBase::CanBatch(const RenderItem& first, const RenderItem& next,
	[[maybe_unused]] const GraphicsRuntimeFeatures& features) const {

	return BackendDrawCommon::CanBatchBasic(first, next);
}

void Engine::BuiltinRenderBackendBase::BeginFrameCommon() {

	constantBufferAllocator_.BeginFrame();
	materialParamBinder_.BeginFrame();
	shaderGraphTimeGPUAddress_ = 0;
}

void Engine::BuiltinRenderBackendBase::SyncAndBindRegistry(const PipelineState& pipelineState,
	const RenderDrawContext& context, ID3D12GraphicsCommandList* commandList) {

	registryAutoBindTable_.Sync(pipelineState, *context.bufferRegistry);
	registryAutoBindTable_.BindGraphics(*context.bufferRegistry, commandList);
	perDrawBindCache_.Sync(pipelineState);
	if (!perDrawBindCache_.Has(shaderGraphTimeCBVSlot_) ||
		!context.graphicsCore || !context.systemContext) {

		return;
	}

	if (shaderGraphTimeGPUAddress_ == 0) {
		const SystemContext& systemContext = *context.systemContext;
		const ShaderGraphTimeConstantsGPU constants{
			.time = systemContext.time,
			.deltaTime = systemContext.deltaTime,
			.smoothDeltaTime = systemContext.smoothDeltaTime,
			.unscaledTime = systemContext.unscaledTime,
		};
		const FrameConstantBufferAllocation allocation =
			constantBufferAllocator_.AllocateAndUpload(context.graphicsCore->GetDXObject().GetResourceRetirement(),
			context.graphicsCore->GetDXObject().GetDevice(),
				constants);
		shaderGraphTimeGPUAddress_ = allocation.gpuAddress;
	}
	if (shaderGraphTimeGPUAddress_ != 0) {
		RootBindingCommand::SetGraphicsCBV(
			commandList,
			perDrawBindCache_.Get(shaderGraphTimeCBVSlot_),
			shaderGraphTimeGPUAddress_);
	}
}

void Engine::BuiltinRenderBackendBase::BindMaterial(const RenderDrawContext& context,
	const PipelineState& pipelineState, const MaterialAsset& material,
	const MaterialParameterSet* overrides,
	ID3D12GraphicsCommandList* commandList) {

	BackendDrawCommon::BindReflectedMaterialParameters(context, materialParamBinder_, pipelineState,
		material, overrides, perDrawBindCache_, materialParamsCBVSlot_, commandList);
	BackendDrawCommon::BindMaterialTextures(context, pipelineState, materialParamBinder_,
		material, commandList, overrides);
}

//============================================================================
//	BuiltinRenderBackendBase classMethods
//============================================================================

namespace Engine {

	BuiltinRenderBackendBase::BuiltinRenderBackendBase() {

		viewCBVSlot_ = perDrawBindCache_.AddSlot("ViewConstants", ShaderBindingKind::CBV);
		shaderGraphTimeCBVSlot_ = perDrawBindCache_.AddSlot(
			"ShaderGraphTimeConstants", ShaderBindingKind::CBV);
		materialParamsCBVSlot_ = perDrawBindCache_.AddSlot(MaterialParameterCBuffer::kSurface, ShaderBindingKind::CBV);
	}
}
