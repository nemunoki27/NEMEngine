#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleModuleRegistry.h>
#include <Engine/Core/Animation/Curves/AnimationCurve.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

namespace Engine {

	//============================================================================
	//	ParticleColorOverLifetimeModule class
	//	寿命の進行度に応じて色をイージング補間する
	//============================================================================
	class ParticleColorOverLifetimeModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleColorOverLifetimeModule() = default;
		~ParticleColorOverLifetimeModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;

		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 発生時の色に掛ける色の始点と終点
		Color4 startColor_ = Color4::White();
		Color4 endColor_ = Color4(1.0f, 1.0f, 1.0f, 0.0f);
		// イージング
		EasingType easingType_ = EasingType::EaseOutSine;

		// カーブで色を制御するか
		bool useCurve_ = false;
		// 進行度から色を返すカーブ
		CurveColor4 curve_{};
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleColorOverLifetimeModule, "ColorOverLifetime");
} // Engine
