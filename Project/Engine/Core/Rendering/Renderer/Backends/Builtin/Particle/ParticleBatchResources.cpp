#include "ParticleBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>

// c++
#include <algorithm>
#include <cassert>
#include <cstring>
#include <span>

//============================================================================
//	ParticleBatchResources classMethods
//============================================================================
void Engine::ParticleBatchResources::Init(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}
	device_ = graphicsCore.GetDXObject().GetDevice();
	geometry_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	materials_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	trailVertices_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	initialized_ = true;
}

void Engine::ParticleBatchResources::EnsureCustomParameterCapacity(uint32_t requiredSize) {

	requiredSize = (std::max)(requiredSize, 16u);
	if (requiredSize <= customParameterCapacity_) {
		return;
	}
	uint32_t newCapacity = 256;
	while (newCapacity < requiredSize) {
		newCapacity *= 2;
	}
	customParameterBuffer_.Reset();
	customParameterMapped_ = nullptr;
	DxUtils::CreateBufferResource(device_, customParameterBuffer_, newCapacity);
	HRESULT hr = customParameterBuffer_->Map(0, nullptr,
		reinterpret_cast<void**>(&customParameterMapped_));
	assert(SUCCEEDED(hr));
	customParameterBuffer_->SetName(L"gParticleCustomParameters");
	customParameterCapacity_ = newCapacity;
}

void Engine::ParticleBatchResources::UploadInstances(const std::vector<ParticleDrawInstanceData>& instances) {

	instanceCount_ = static_cast<uint32_t>(instances.size());
	if (instanceCount_ == 0) {
		return;
	}
	std::vector<ParticleGeometryData> geometry;
	std::vector<ParticleMaterialData> materials;
	geometry.reserve(instances.size());
	materials.reserve(instances.size());
	for (const ParticleDrawInstanceData& instance : instances) {
		geometry.emplace_back(instance.geometry);
		materials.emplace_back(instance.material);
	}
	geometry_.Upload(std::span<const ParticleGeometryData>(geometry.data(), geometry.size()));
	materials_.Upload(std::span<const ParticleMaterialData>(materials.data(), materials.size()));
}

void Engine::ParticleBatchResources::UploadCustomParameters(const std::vector<uint8_t>& data) {

	EnsureCustomParameterCapacity(static_cast<uint32_t>(data.size()));
	if (!data.empty()) {
		std::memcpy(customParameterMapped_, data.data(), data.size());
	} else {
		std::memset(customParameterMapped_, 0, 16);
	}
}

void Engine::ParticleBatchResources::UploadTrailVertices(const std::vector<ParticleTrailVertex>& vertices) {

	trailVertexCount_ = static_cast<uint32_t>(vertices.size());
	if (trailVertexCount_ == 0) {
		return;
	}
	trailVertices_.Upload(std::span<const ParticleTrailVertex>(vertices.data(), vertices.size()));
}
