#pragma once

#include "IParticleModuleDrawer.h"
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleCustomShaderParameterModule.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>
#include <Engine/Editor/Animation/Curves/CurveGenerator.h>

namespace Engine {

	//============================================================================
	//	ParticleCustomShaderParameterModuleDrawer class
	//	Shader入力の編集表示とカーブ選択を所有する
	//============================================================================
	class ParticleCustomShaderParameterModuleDrawer : public IParticleModuleDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		bool Draw(IParticleModule& module) override;
		void SetReflectedParameters(ParticleCustomShaderParameterModule& module, const std::vector<ShaderConstantBufferVariable>& parameters);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		struct ParameterUIState {

			CurveEditorState curveState{};
			CurveGeneratorState curve3GeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
			CurveGeneratorState curveWGeneratorState_{ .fixedTimeRange = true, .maxKeyTime = 1.0f };
		};

		std::vector<ShaderConstantBufferVariable> reflectedParameters_{};
		std::unordered_map<std::string, ParameterUIState> uiStates_{};

		// 定数と寿命カーブを編集する
		bool DrawParameter(const ShaderConstantBufferVariable& variable,
			ParticleMaterialAnimatedParameter& parameter, ParameterUIState& uiState);
	};
}
