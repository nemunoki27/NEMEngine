#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUState.h"

namespace Engine {

	struct ParticleGPUSimulationConstants {

		Matrix4x4 emitterWorld = Matrix4x4::Identity();
		Vector4 offsetMinimum = Vector4(0, 0, 0, 0);
		Vector4 offsetMaximum = Vector4(0, 0, 0, 0);
		Vector4 speedTime = Vector4(0, 0, 0, 0);
		Vector4 shapeParameters = Vector4(0, 0, 0, 0);
		Vector4 shapeSize = Vector4(0, 0, 0, 0);
		Vector4 pointDirection = Vector4(0, 1, 0, 0);
		Vector4 circle = Vector4(0, 360, 10, 1);
		uint32_t capacity = 0;
		uint32_t phaseCount = 0;
		uint32_t spawnCount = 0;
		uint32_t seed = 0;
		uint32_t birthLow = 0;
		uint32_t birthHigh = 0;
		uint32_t shape = 0;
		uint32_t operation = 0;
		uint32_t faceMask = 0;
		uint32_t spawnMode = 0;
		uint32_t velocityMode = 0;
		uint32_t preserveWrapDirection = 0;
		uint32_t moduleStateCount = 0;
		uint32_t spawnDirectionSlot = UINT32_MAX;
		uint32_t shapeStateSlot = UINT32_MAX;
		uint32_t initialShapeOffset = 0;
	};
	static_assert(sizeof(ParticleGPUSimulationConstants) == 240);

	struct ParticleGPUViewConstants {

		Matrix4x4 viewProjection = Matrix4x4::Identity();
		Vector4 cameraPosition = Vector4(0, 0, 0, 0);
		Vector4 cameraForward = Vector4(0, 0, 1, 0);
		Vector4 frustum[6]{};
		uint32_t capacity = 0;
		uint32_t phaseCount = 0;
		uint32_t indexCount = 0;
		uint32_t operation = 0;
		uint32_t radixShift = 0;
		uint32_t groupCount = 0;
		uint32_t sortEnabled = 0;
		uint32_t phaseIndex = 0;
		uint32_t billboardMask = 0;
		uint32_t is2D = 0;
		uint32_t blendMode = 0;
		uint32_t flipV = 0;
		float geometryRadius = 1;
		uint32_t shapeStateSlot = UINT32_MAX;
		uint32_t moduleStateCount = 0;
		uint32_t geometryShape = 0;
	};
	static_assert(sizeof(ParticleGPUViewConstants) == 256);

	struct ParticleGPUShapeConstants {

		ParticleShapeData shape{};
		uint32_t uvMode = 0;
		uint32_t reserved[3]{};
	};
	static_assert(sizeof(ParticleGPUShapeConstants) == 96);

	// 発生値を定数バッファへまとめる
	ParticleGPUSimulationConstants MakeParticleGPUSimulationConstants(const ParticleGPUSimulationStep& step);
}
