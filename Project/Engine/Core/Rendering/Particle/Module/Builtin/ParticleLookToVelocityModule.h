#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleLookToVelocityModule class
	//	進行方向に回転を向ける
	//============================================================================
	class ParticleLookToVelocityModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleLookToVelocityModule() = default;
		~ParticleLookToVelocityModule() = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		bool DrawImGui() override;

		ParticleModuleExecutionMode GetSpawnExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		ParticleModuleExecutionMode GetUpdateExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		void OnSpawn(Particle& particle) override;
		void OnUpdate(Particle& particle, float deltaTime) override;
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleLookToVelocityModule, "LookToVelocity");
} // Engine
