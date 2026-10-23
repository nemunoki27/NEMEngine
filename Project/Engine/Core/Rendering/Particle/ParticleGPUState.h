#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUProgram.h"

namespace Engine {

	// CPUとGPUで同じ順序に配置する粒子属性
	struct ParticleAttributesGPU {

		Vector4 positionAge = Vector4(0, 0, 0, 0);
		Vector4 velocityLifetime = Vector4(0, 0, 0, 1);
		Vector4 rotation = Vector4(0, 0, 0, 1);
		Vector4 color = Vector4(1, 1, 1, 1);
		Vector4 scaleSize = Vector4(1, 1, 1, 1);
		Vector4 emissive = Vector4(0, 0, 0, 0);
		Vector4 uv = Vector4(0, 0, 1, 1);
		Vector4 uvRotationPivotAlpha = Vector4(0, 0, 0, 0);
		uint32_t alive = 0;
		uint32_t phase = 0;
		uint32_t birthLow = 0;
		uint32_t birthHigh = 0;
		uint32_t hasParent = 0;
		uint32_t parentPhase = 0;
		uint32_t trailIndex = UINT32_MAX;
		uint32_t trailGeneration = 0;
	};
	static_assert(sizeof(ParticleAttributesGPU) == 160);

	struct ParticleParentGPU {

		Matrix4x4 matrix = Matrix4x4::Identity();
		Matrix4x4 inverse = Matrix4x4::Identity();
		Vector4 rotation = Vector4(0, 0, 0, 1);
		Vector4 scale = Vector4(1, 1, 1, 0);
		Matrix4x4 previousMatrix = Matrix4x4::Identity();
		Vector4 previousRotation = Vector4(0, 0, 0, 1);
		Vector4 previousScale = Vector4(1, 1, 1, 0);
		uint32_t valid = 0;
		uint32_t previousValid = 0;
		uint32_t keepWorldOnDetach = 1;
		uint32_t reserved = 0;
	};
	static_assert(sizeof(ParticleParentGPU) == 272);

	struct ParticleStatisticsGPU {

		uint32_t particleCount = 0;
		uint32_t trailCount = 0;
		uint32_t spawnClaims = 0;
		uint32_t overflowCount = 0;
	};
	static_assert(sizeof(ParticleStatisticsGPU) == 16);

	// Worldの更新1回に対応するGPU要求
	struct ParticleGPUSimulationStep {

		std::shared_ptr<const ParticleSystemDefinition> definition;
		std::shared_ptr<const ParticleGPUProgram> program;
		Matrix4x4 emitterWorld = Matrix4x4::Identity();
		std::vector<ParticleParentGPU> parents;
		uint64_t birthSequence = 0;
		uint64_t sequence = 0;
		uint32_t spawnCount = 0;
		uint32_t seed = 0;
		float deltaTime = 0.0f;
	};

	// Worldは要求と遅延した結果を持ち、描画側はGPU資源を所有する
	struct ParticleGPUState {

		std::shared_ptr<const ParticleSystemDefinition> definition;
		std::shared_ptr<const ParticleGPUProgram> program;
		std::vector<ParticleGPUSimulationStep> pending;
		std::vector<ParticleParentGPU> queuedParents;
		ParticleStatisticsGPU statistics;
		uint64_t epoch = 1;
		uint64_t lastSubmittedSequence = 0;
		uint64_t completedSequence = 0;
		uint64_t nextSequence = 0;
		uint64_t emissionEndSequence = UINT64_MAX;
		bool statisticsValid = false;
		bool rejected = false;
		std::string diagnostic;

		// 実行不可の理由を残して保留要求を破棄する
		void Reject(const std::string& reason);
	};
}
