#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleMaterialStructures.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>
#include <Engine/Editor/Animation/Curves/CurveGenerator.h>

// c++
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	ParticleCustomShaderParameterModule class
	//	シェーダー固有パラメータを粒子寿命で制御する
	//============================================================================
	class ParticleCustomShaderParameterModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleCustomShaderParameterModule() = default;
		~ParticleCustomShaderParameterModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		bool DrawImGui() override;

		//--------- accessor -----------------------------------------------------

		void SetReflectedParameters(const std::vector<ShaderConstantBufferVariable>& parameters);
		const std::unordered_map<std::string, ParticleMaterialAnimatedParameter>& GetParameters() const { return parameters_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct ParameterUiState {

			CurveEditorState curveState{};
			CurveGeneratorState curve3GeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
			CurveGeneratorState curveWGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
		};

		//--------- variables ----------------------------------------------------

		std::vector<ShaderConstantBufferVariable> reflectedParameters_{};
		std::unordered_map<std::string, ParticleMaterialAnimatedParameter> parameters_{};
		std::unordered_map<std::string, ParameterUiState> uiStates_{};

		//--------- functions ----------------------------------------------------

		// パラメータの定数と寿命アニメーションを描画する
		bool DrawParameter(const ShaderConstantBufferVariable& variable,
			ParticleMaterialAnimatedParameter& parameter, ParameterUiState& uiState);
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleCustomShaderParameterModule, "CustomShaderParameter");
} // Engine
