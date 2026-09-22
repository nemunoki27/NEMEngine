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

		// 保存と実行に使う設定
		struct Settings {

			// 速度へ加算する重力
			Vector3 gravity = Vector3(0.0f, -9.8f, 0.0f);

			// 地面で反射させるか
			bool reflectGround = false;
			// TODO: エミッターの位置を地面の高さに自動設定する
			bool autoGroundEmitter = false;
			// 地面の高さ
			float reflectGroundY = 0.0f;
			// 反発係数
			float restitution = 0.4f;
		};

		//========================================================================
		//	public Methods
		//========================================================================

		ParticleGravityForceModule() = default;
		~ParticleGravityForceModule() override = default;

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
