#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleModuleRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleRotationOverLifetimeModule class
	//	ビルボード面内の回転を初期値と回転速度で進める
	//============================================================================
	class ParticleRotationOverLifetimeModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleRotationOverLifetimeModule() = default;
		~ParticleRotationOverLifetimeModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;

		void OnSpawn(std::span<Particle> newborn) override;
		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 初期回転の範囲、度数法
		float initialMin_ = 0.0f;
		float initialMax_ = 360.0f;
		// 回転速度の範囲、度数法
		float speedMin_ = -90.0f;
		float speedMax_ = 90.0f;
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleRotationOverLifetimeModule, "RotationOverLifetime");
} // Engine
