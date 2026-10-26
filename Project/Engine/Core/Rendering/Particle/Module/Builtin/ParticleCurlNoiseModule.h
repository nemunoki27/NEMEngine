#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleCurlNoiseModule class
	//	ノイズの回転を速度へ加えて渦のある流れを作る
	//============================================================================
	class ParticleCurlNoiseModule : public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		struct Settings {

			float strength = 1.0f;
			float frequency = 1.0f;
			Vector3 scrollSpeed{};
		};

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		ParticleModuleExecutionMode GetUpdateExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		void OnUpdate(Particle& particle, float deltaTime) override;

		//--------- accessor -----------------------------------------------------

		const Settings& GetSettings() const { return settings_; }
		void SetSettings(const Settings& settings) { settings_ = settings; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Settings settings_{};
	};
}
