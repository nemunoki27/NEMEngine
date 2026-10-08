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

		// 保存と実行に使う設定
		struct Settings {

			// 力の強さ
			float strength = 1.0f;
			// ノイズの周波数
			float frequency = 1.0f;
		};

		//========================================================================
		//	public Methods
		//========================================================================

		ParticleNoiseForceModule() = default;
		~ParticleNoiseForceModule() override = default;

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
