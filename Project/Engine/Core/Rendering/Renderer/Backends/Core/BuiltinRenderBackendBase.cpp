#include "BuiltinRenderBackendBase.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>

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
}

void Engine::BuiltinRenderBackendBase::SyncAndBindRegistry(const PipelineState& pipelineState,
	const RenderDrawContext& context, ID3D12GraphicsCommandList* commandList) {

	registryAutoBindTable_.Sync(pipelineState, *context.bufferRegistry);
	registryAutoBindTable_.BindGraphics(*context.bufferRegistry, commandList);
	perDrawBindCache_.Sync(pipelineState);
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
