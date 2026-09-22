#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleNoiseUVModule class
	//	UVオフセットをノイズで揺らす
	//============================================================================
	class ParticleNoiseUVModule :
		public IParticleModule {
	public:

		// 保存と実行に使う設定
		struct Settings {

			// 揺れの強さ
			float strength = 0.1f;
			// ノイズの周波数
			float frequency = 1.0f;
		};

		//========================================================================
		//	public Methods
		//========================================================================

		ParticleNoiseUVModule() = default;
		~ParticleNoiseUVModule() override = default;

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

} // Engine
