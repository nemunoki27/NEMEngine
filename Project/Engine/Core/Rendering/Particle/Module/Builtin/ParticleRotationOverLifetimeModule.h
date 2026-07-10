#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleRotationOverLifetimeModule class
	//	角速度ベクトルで回転
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
		bool DrawImGui() override;

		void OnSpawn(std::span<Particle> newborn) override;
		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 初期回転のランダム範囲、度数法で発生時に一度だけクォータニオンへ変換する
		Vector3 initialMin_ = Vector3::AnyInit(0.0f);
		Vector3 initialMax_ = Vector3(0.0f, 0.0f, 360.0f);
		// 角速度ベクトルのランダム範囲、軸x速さで度数法
		Vector3 speedMin_ = Vector3(0.0f, 0.0f, -90.0f);
		Vector3 speedMax_ = Vector3(0.0f, 0.0f, 90.0f);
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleRotationOverLifetimeModule, "RotationOverLifetime");
} // Engine
