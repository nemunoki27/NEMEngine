#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleModuleRegistry.h>
#include <Engine/Core/Animation/Curves/AnimationCurve.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

namespace Engine {

	//============================================================================
	//	ParticleSizeOverLifetimeModule class
	//	寿命の進行度に応じて大きさをイージング補間する
	//============================================================================
	class ParticleSizeOverLifetimeModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleSizeOverLifetimeModule() = default;
		~ParticleSizeOverLifetimeModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;

		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 発生時の大きさに掛ける倍率の始点と終点
		float startScale_ = 1.0f;
		float endScale_ = 0.0f;
		// イージング
		EasingType easingType_ = EasingType::EaseOutSine;

		// カーブで倍率を制御するか
		bool useCurve_ = false;
		// 進行度から倍率を返すカーブ
		CurveFloat curve_{};
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleSizeOverLifetimeModule, "SizeOverLifetime");
} // Engine
