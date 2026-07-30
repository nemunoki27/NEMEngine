#include "ParticleBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>

// c++
#include <algorithm>
#include <span>

//============================================================================
//	ParticleTrailRenderData structureMethods
//============================================================================
void Engine::ParticleTrailRenderData::Clear() {

	points.clear();
	segments.clear();
	materials.clear();
	customParameters.clear();
}

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
	trailPoints_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	trailSegments_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	trailMaterials_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	initialized_ = true;
}

void Engine::ParticleBatchResources::EnsureCustomParameterCapacity(uint32_t requiredSize) {

	customParameterBuffer_.EnsureCapacity(device_,
		(std::max)(requiredSize, 16u), "gParticleCustomParameters");
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
		customParameterBuffer_.Write(data.data(), data.size());
	} else {
		const std::array<uint8_t, 16> emptyData{};
		customParameterBuffer_.Write(emptyData.data(), emptyData.size());
	}
}

void Engine::ParticleBatchResources::UploadTrailGeometry(const ParticleTrailRenderData& data) {

	trailSegmentCount_ = static_cast<uint32_t>(data.segments.size());
	if (trailSegmentCount_ == 0) {
		return;
	}
	trailPoints_.Upload(std::span<const ParticleTrailPointData>(data.points.data(), data.points.size()));
	trailSegments_.Upload(std::span<const uint32_t>(data.segments.data(), data.segments.size()));
}

void Engine::ParticleBatchResources::EnsureTrailCustomParameterCapacity(uint32_t requiredSize) {

	trailCustomParameterBuffer_.EnsureCapacity(device_,
		(std::max)(requiredSize, 16u),
		"gParticleCustomParameters_Trail");
}

void Engine::ParticleBatchResources::UploadTrailMaterials(
	const std::vector<ParticleMaterialData>& materials) {

	if (materials.empty()) {
		trailMaterials_.EnsureCapacity(1);
		return;
	}
	trailMaterials_.Upload(std::span<const ParticleMaterialData>(materials.data(), materials.size()));
}

void Engine::ParticleBatchResources::UploadTrailCustomParameters(const std::vector<uint8_t>& data) {

	EnsureTrailCustomParameterCapacity(static_cast<uint32_t>(data.size()));
	if (!data.empty()) {
		trailCustomParameterBuffer_.Write(data.data(), data.size());
	} else {
		const std::array<uint8_t, 16> emptyData{};
		trailCustomParameterBuffer_.Write(
			emptyData.data(), emptyData.size());
	}
}
