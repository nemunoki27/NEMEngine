#include "ParticleBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>

// c++
#include <span>

//============================================================================
//	ParticleBatchResources classMethods
//============================================================================
void Engine::ParticleBatchResources::Init(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}
	instances_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	trailVertices_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	initialized_ = true;
}

void Engine::ParticleBatchResources::UploadInstances(const std::vector<ParticleInstanceData>& instances) {

	instanceCount_ = static_cast<uint32_t>(instances.size());
	if (instanceCount_ == 0) {
		return;
	}
	instances_.Upload(std::span<const ParticleInstanceData>(instances.data(), instances.size()));
}

void Engine::ParticleBatchResources::UploadTrailVertices(const std::vector<ParticleTrailVertex>& vertices) {

	trailVertexCount_ = static_cast<uint32_t>(vertices.size());
	if (trailVertexCount_ == 0) {
		return;
	}
	trailVertices_.Upload(std::span<const ParticleTrailVertex>(vertices.data(), vertices.size()));
}
