#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Particle/Module/Base/IParticleModule.h>

// c++
#include <filesystem>
#include <memory>
#include <vector>

namespace Engine {

	struct ParticleModuleExecutionGroup {

		ParticleModuleExecutionMode mode = ParticleModuleExecutionMode::None;
		std::vector<IParticleModule*> modules;
	};

	struct ParticlePhaseDefinition {

		ParticleValue<float> lifetime{ 1.0f };
		ParticleLifeEndMode lifeEndMode = ParticleLifeEndMode::Kill;
		ParticlePhaseParentSettings parentSettings{};
		std::vector<std::unique_ptr<IParticleModule>> modules;
		std::vector<ParticleModuleExecutionGroup> spawnExecution;
		std::vector<ParticleModuleExecutionGroup> updateExecution;
		bool hasSpawnBatch = false;
		bool hasUpdateBatch = false;
	};

	struct ParticleGroupDefinition {

		UUID id{};
		std::vector<ParticlePhaseDefinition> phases;
		bool hasUpdateBatch = false;
	};

	struct ParticleParentPose {

		Matrix4x4 matrix = Matrix4x4::Identity();
		Quaternion rotation = Quaternion::Identity();
		Vector3 scale = Vector3::AnyInit(1.0f);
		bool resolved = false;
	};

	struct ParticleEffectDefinition {

		ParticleEffectAsset asset{};
		std::vector<ParticleGroupDefinition> groups;
		bool valid = false;
		uint64_t revision = 1;

		// ホットリロード用のファイル情報
		std::filesystem::path path{};
		std::filesystem::file_time_type lastWriteTime{};
		// エディター編集の適用済みバージョン
		uint64_t appliedEditVersion = 0;
	};
}
