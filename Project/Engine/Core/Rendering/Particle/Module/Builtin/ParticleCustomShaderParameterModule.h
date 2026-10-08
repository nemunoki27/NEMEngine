#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleMaterialStructures.h>

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

		//--------- accessor -----------------------------------------------------

		void SetParameters(const std::unordered_map<std::string, ParticleMaterialAnimatedParameter>& parameters);
		const std::unordered_map<std::string, ParticleMaterialAnimatedParameter>& GetParameters() const { return parameters_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::unordered_map<std::string, ParticleMaterialAnimatedParameter> parameters_{};

	};

} // Engine
