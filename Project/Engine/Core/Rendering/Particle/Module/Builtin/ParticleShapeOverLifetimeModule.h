#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/Parametric/ParticleParametricShapeRegistry.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleShapeAnimationSettings.h>

// c++
#include <array>
#include <string>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	ParticleShapeOverLifetimeModule class
	//	形状パラメータを項目ごとに寿命アニメーションする、パラメトリック形状のみ対応
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

		ParticleModuleExecutionMode GetSpawnExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		ParticleModuleExecutionMode GetUpdateExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		void OnSpawn(Particle& particle) override;
		void OnUpdate(Particle& particle, float deltaTime) override;

		//--------- accessor -----------------------------------------------------

		const ParticleShapeAnimationSettings& GetSettings() const { return settings_; }
		void SetSettings(const ParticleShapeAnimationSettings& settings);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 対象形状、パラメトリック形状のみ対応
		ParticleShapeAnimationSettings settings_{};
		// 実行時に名前検索を行わないための参照
		std::array<const ParticleMaterialAnimatedParameter*, 14> parameterCache_{};

		//--------- functions ----------------------------------------------------

		// 実行時評価用の参照を名前付きパラメータから解決
		void RebuildParameterCache();

		// 寿命進行度から形状を評価
		void EvaluateShape(Particle& particle, float progress) const;
	};

} // Engine
