#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleSystemSettings.h>
#include <Engine/Core/Rendering/Particle/Module/Base/IParticleModule.h>

// c++
#include <memory>
#include <vector>

namespace Engine {

	struct ParticleModuleExecutionGroup {

		ParticleModuleExecutionMode mode = ParticleModuleExecutionMode::None;
		std::vector<IParticleModule*> modules;
	};

	struct ParticlePhaseDefinition {

		ParticleValue<float> lifetime{1.0f};
		ParticleLifeEndMode lifeEndMode = ParticleLifeEndMode::Kill;
		ParticlePhaseParentSettings parentSettings{};
		std::vector<std::unique_ptr<IParticleModule>> modules;
		std::vector<ParticleModuleExecutionGroup> spawnExecution;
		std::vector<ParticleModuleExecutionGroup> updateExecution;
		bool hasSpawnBatch = false;
		bool hasUpdateBatch = false;
	};

	// 設定のSnapshotと、1つのSystemの実行順を共有する
	struct ParticleSystemDefinition {

		std::shared_ptr<const ParticleSystemSettings> settings;
		std::vector<ParticlePhaseDefinition> phases;
		ParticleRenderSettings renderSettings;
		uint64_t structure = 0;
		uint32_t stateCount = 0;
		bool hasUpdateBatch = false;
	};

	struct ParticleParentPose {

		Matrix4x4 matrix = Matrix4x4::Identity();
		Quaternion rotation = Quaternion::Identity();
		Vector3 scale = Vector3::AnyInit(1.0f);
		bool resolved = false;
	};
}
