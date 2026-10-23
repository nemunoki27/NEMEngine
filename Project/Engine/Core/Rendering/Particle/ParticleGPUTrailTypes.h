#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUState.h"

namespace Engine {

	struct ParticleTrailPointGPU {

		Vector4 positionAge = Vector4(0, 0, 0, 0);
		Vector4 color = Vector4(1, 1, 1, 1);
		Vector4 emissive = Vector4(0, 0, 0, 0);
		uint32_t birthLow = 0;
		uint32_t birthHigh = 0;
		float distance = 0;
		float reserved = 0;
	};
	static_assert(sizeof(ParticleTrailPointGPU) == 64);

	struct ParticleTrailGPU {

		uint32_t ownerIndex = UINT32_MAX;
		uint32_t ownerBirthLow = 0;
		uint32_t ownerBirthHigh = 0;
		uint32_t generation = 1;
		uint32_t active = 0;
		uint32_t begin = 0;
		uint32_t count = 0;
		uint32_t reserved = 0;
		ParticleTrailPointGPU head;
	};
	static_assert(sizeof(ParticleTrailGPU) == 96);

	struct ParticleUInt4GPU {

		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t z = 0;
		uint32_t w = 0;
	};
	static_assert(sizeof(ParticleUInt4GPU) == 16);

	struct ParticleGPUTrailConstants {

		uint32_t trailCapacity = 0;
		uint32_t pointCapacity = 0;
		uint32_t particleCapacity = 0;
		uint32_t operation = 0;
		uint32_t mode = 0;
		uint32_t keepAfterDeath = 0;
		uint32_t textureMode = 0;
		uint32_t ribbonSortCount = 0;
		float minimumDistance = 0;
		float lifetime = 1;
		float deltaTime = 0;
		float tileLength = 1;
		float startWidth = 0.1f;
		float endWidth = 0.1f;
		uint32_t pointModuloHighFactor = 0;
		uint32_t reserved = 0;
		Color4 startColor = Color4::White();
		Color4 endColor = Color4(1, 1, 1, 0);
	};
	static_assert(sizeof(ParticleGPUTrailConstants) == 96);

	// 軌跡の容量と見た目をShaderへ渡す
	ParticleGPUTrailConstants MakeParticleGPUTrailConstants(const ParticleTrailSettings& settings, uint32_t particleCapacity);
}
