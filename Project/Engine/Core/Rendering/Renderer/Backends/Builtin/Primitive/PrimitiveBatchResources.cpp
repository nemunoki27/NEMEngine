#include "PrimitiveBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>

// c++
#include <span>

//============================================================================
//	PrimitiveBatchResources classMethods
//============================================================================
void Engine::PrimitiveBatchResources::Init(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}
	instances_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	initialized_ = true;
}

void Engine::PrimitiveBatchResources::UploadInstances(const std::vector<PrimitiveInstanceData>& instances) {

	instanceCount_ = static_cast<uint32_t>(instances.size());
	if (instanceCount_ == 0) {
		return;
	}
	instances_.Upload(std::span<const PrimitiveInstanceData>(instances.data(), instances.size()));
}
