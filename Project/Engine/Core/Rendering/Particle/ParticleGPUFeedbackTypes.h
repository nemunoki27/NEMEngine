#pragma once

//============================================================================
//	include
//============================================================================
#include "Structures/ParticleFeedbackSettings.h"
#include <Engine/Core/Rendering/Decals/DecalTypes.h>

// c++
#include <span>

namespace Engine {

	struct ParticleGPUIdentity {

		Entity entity{};
		uint64_t token = 0;
		uint64_t epoch = 0;
	};
	static_assert(sizeof(ParticleGPUIdentity) == 24);

	struct ParticleGPUEvent {

		ParticleGPUIdentity source{};
		ParticleGPUIdentity target{};
		Vector4 positionRadius{};
		Vector4 normalHardness{};
		Vector4 velocityLifetime{};
		Vector4 color{};
		Vector2 uv{};
		uint32_t seed = 0;
		uint32_t flags = 0;
	};
	static_assert(sizeof(ParticleGPUEvent) == 128);

	struct ParticleGPUInheritedState {

		Vector4 color{1, 1, 1, 1};
		uint32_t seed = 0;
		uint32_t flags = 0;
		uint32_t reserved[2]{};
	};
	static_assert(sizeof(ParticleGPUInheritedState) == 32);

	struct ParticleGPUEventRoute {

		ParticleGPUIdentity target{};
		uint32_t kind = 0;
		uint32_t phase = UINT32_MAX;
		uint32_t count = 1;
		uint32_t flags = 0;
		float probability = 1.0f;
		float time = 0.0f;
		Vector4 color{1, 1, 1, 1};
		Vector4 parameters{};
	};
	static_assert(sizeof(ParticleGPUEventRoute) == 80);

	struct ParticleGPUEventConstants {

		ParticleGPUIdentity source{};
		uint32_t routeCount = 0;
		uint32_t eventCapacity = 0;
		uint32_t systemCount = 0;
		uint32_t childCapacity = 0;
		uint32_t operation = 0;
		uint32_t reserved = 0;
	};
	static_assert(sizeof(ParticleGPUEventConstants) == 48);

	struct ParticlePaintReceiverGPU {

		Matrix4x4 worldToReceiver = Matrix4x4::Identity();
		Vector4 scaleOpacity{1, 1, 1, 0};
		Entity entity{};
		uint32_t texture = UINT32_MAX;
		uint32_t projection = 0;
	};
	static_assert(sizeof(ParticlePaintReceiverGPU) == 96);

	struct ParticleImpactDecalGPU {

		DecalGPUData decal{};
		Vector4 ageLifetimeFade{};
		uint32_t alive = 0;
		Vector3 center{};
	};
	static_assert(sizeof(ParticleImpactDecalGPU) == 256);

	struct ParticleFeedbackStatisticsGPU {

		uint32_t emitted = 0;
		uint32_t eventOverflow = 0;
		uint32_t childOverflow = 0;
		uint32_t decalOverflow = 0;
		uint32_t paintOverflow = 0;
		uint32_t invalidReceiver = 0;
		uint32_t activeDecals = 0;
		uint32_t reserved = 0;
	};
	static_assert(sizeof(ParticleFeedbackStatisticsGPU) == 32);

	// 循環する発生経路を検出する
	bool HasParticleEventCycle(std::span<const Entity> sources, std::span<const std::vector<Entity>> targets);
}
