#include "ParticleGPUTrailTypes.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <bit>

Engine::ParticleGPUTrailConstants Engine::MakeParticleGPUTrailConstants(
	const ParticleTrailSettings& settings, uint32_t particleCapacity) {

	ParticleGPUTrailConstants constants;
	constants.pointCapacity = static_cast<uint32_t>(std::clamp(settings.maxPoints, 2, 1024));
	const uint64_t budget = uint64_t(settings.memoryBudgetMB) * 1024 * 1024;
	const uint64_t elements = std::bit_floor(budget / sizeof(ParticleTrailPointGPU));
	constants.trailCapacity = static_cast<uint32_t>((std::min)(uint64_t(settings.maxTrails),
		elements / constants.pointCapacity));
	if (settings.mode == ParticleTrailMode::Ribbon) constants.trailCapacity = (std::min)(constants.trailCapacity, 1u);
	constants.particleCapacity = particleCapacity;
	constants.mode = static_cast<uint32_t>(settings.mode);
	constants.keepAfterDeath = settings.keepAfterParticleDeath ? 1u : 0u;
	constants.textureMode = static_cast<uint32_t>(settings.textureMode);
	constants.ribbonSortCount = 1;
	while (constants.ribbonSortCount < constants.pointCapacity) constants.ribbonSortCount <<= 1;
	constants.minimumDistance = settings.minDistance;
	constants.lifetime = settings.pointLifetime;
	constants.tileLength = settings.tileLength;
	constants.startWidth = settings.startWidth;
	constants.endWidth = settings.endWidth;
	constants.pointModuloHighFactor = static_cast<uint32_t>((1ull << 32) % constants.pointCapacity);
	constants.startColor = settings.startColor;
	constants.endColor = settings.endColor;
	return constants;
}
