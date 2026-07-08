#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleModuleRegistry.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

namespace Engine {

	//============================================================================
	//	ParticleShapeOverLifetimeModule class
	//	形状パラメータを寿命の進行度でイージング補間する、Ring/Cylinderのみ対応
	//============================================================================
	class ParticleShapeOverLifetimeModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleShapeOverLifetimeModule() = default;
		~ParticleShapeOverLifetimeModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;

		void OnSpawn(std::span<Particle> newborn) override;
		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 対象形状、Ring/Cylinderのみ対応
		PrimitiveType shape_ = PrimitiveType::Ring;
		// 形状パラメータの始点と終点
		PrimitiveRingParams ringStart_{};
		PrimitiveRingParams ringEnd_{};
		PrimitiveCylinderParams cylinderStart_{};
		PrimitiveCylinderParams cylinderEnd_{};
		// イージング
		EasingType easingType_ = EasingType::EaseOutSine;

		//--------- functions ----------------------------------------------------

		// 進行度から形状パラメータを補間して粒子へ書き込む
		void ApplyShapeParams(Particle& particle, float easedT) const;
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleShapeOverLifetimeModule, "ShapeOverLifetime");
} // Engine
