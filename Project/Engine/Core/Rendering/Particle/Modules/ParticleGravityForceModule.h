#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleModuleRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleGravityForceModule class
	//	重力を速度へ加算する
	//============================================================================
	class ParticleGravityForceModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleGravityForceModule() = default;
		~ParticleGravityForceModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;

		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 重力
		Vector3 gravity_ = Vector3(0.0f, -9.8f, 0.0f);
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleGravityForceModule, "GravityForce");
} // Engine
