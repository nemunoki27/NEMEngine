#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleNoiseForceModule class
	//	位置に応じたノイズの力を速度へ加算して乱流を表現する
	//============================================================================
	class ParticleNoiseForceModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleNoiseForceModule() = default;
		~ParticleNoiseForceModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		bool DrawImGui();

		ParticleModuleExecutionMode GetUpdateExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		void OnUpdate(Particle& particle, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 力の強さ
		float strength_ = 1.0f;
		// ノイズの周波数
		float frequency_ = 1.0f;
	};

} // Engine
