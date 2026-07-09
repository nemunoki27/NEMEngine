#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/Parametric/ParticleParametricShapeRegistry.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

namespace Engine {

	//============================================================================
	//	ParticleShapeOverLifetimeModule class
	//	形状パラメータを寿命の進行度でイージング補間する、パラメトリック形状のみ対応
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
		bool DrawImGui() override;

		void OnSpawn(std::span<Particle> newborn) override;
		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 対象形状、パラメトリック形状のみ対応
		PrimitiveType shape_ = PrimitiveType::Ring;
		// 形状ごとのパラメータ、キーは各形状が解釈する
		nlohmann::json params_ = nlohmann::json::object();
		// 補間するshapeParamsの始点と終点
		Vector4 shapeStart_{};
		Vector4 shapeEnd_{};
		// イージング
		EasingType easingType_ = EasingType::EaseOutSine;

		//--------- functions ----------------------------------------------------

		// 形状パラメータから始点と終点を詰め直す
		void PackShapeParams();
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleShapeOverLifetimeModule, "ShapeOverLifetime");
} // Engine
