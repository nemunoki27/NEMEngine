#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleGravityForceModule class
	//	速度へ重力を加算する、地面での反射にも対応する
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
		bool DrawImGui() override;

		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 速度へ加算する重力
		Vector3 gravity_ = Vector3(0.0f, -9.8f, 0.0f);

		// 地面で反射させるか
		bool reflectGround_ = false;
		// 地面の高さ
		float reflectGroundY_ = 0.0f;
		// 反発係数
		float restitution_ = 0.4f;
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleGravityForceModule, "GravityForce");
} // Engine
