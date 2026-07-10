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

		void OnSpawn(std::span<Particle> newborn) override;
		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleLookToVelocityModule, "LookToVelocity");
} // Engine