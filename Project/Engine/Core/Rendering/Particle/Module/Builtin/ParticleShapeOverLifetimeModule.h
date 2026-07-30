#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/Parametric/ParticleParametricShapeRegistry.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleMaterialStructures.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>
#include <Engine/Editor/Animation/Curves/CurveGenerator.h>

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
		bool DrawImGui();

		ParticleModuleExecutionMode GetSpawnExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		ParticleModuleExecutionMode GetUpdateExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		void OnSpawn(Particle& particle) override;
		void OnUpdate(Particle& particle, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct ParameterUiState {

			CurveEditorState curveState{};
			CurveGeneratorState curveGeneratorState{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
			CurveGeneratorState alphaGeneratorState{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
		};

		//--------- variables ----------------------------------------------------

		// 対象形状、パラメトリック形状のみ対応
		PrimitiveType shape_ = PrimitiveType::Ring;
		// 形状項目ごとのアニメーション
		std::unordered_map<std::string, ParticleMaterialAnimatedParameter> parameters_{};
		// 実行時に名前検索を行わないための参照
		std::array<const ParticleMaterialAnimatedParameter*, 14> parameterCache_{};
		// カーブ編集の状態
		std::unordered_map<std::string, ParameterUiState> uiStates_{};

		//--------- functions ----------------------------------------------------

		// 選択形状に必要なパラメータを補完
		void EnsureParameters();
		// 実行時評価用の参照を名前付きパラメータから解決
		void RebuildParameterCache();
		// 形状項目のアニメーションUIを描画
		bool DrawParameter(const char* label, const char* key, float defaultValue,
			float minValue, float maxValue, float dragSpeed = 0.01f);
		// 色項目のアニメーションUIを描画
		bool DrawColorParameter(const char* label, const char* key, const Color4& defaultValue);
		// 寿命進行度から形状を評価
		void EvaluateShape(Particle& particle, float progress) const;
	};

} // Engine
