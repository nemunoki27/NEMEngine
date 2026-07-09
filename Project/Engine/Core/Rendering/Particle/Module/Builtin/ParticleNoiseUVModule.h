#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleNoiseUVModule class
	//	UVオフセットをノイズで揺らす
	//============================================================================
	class ParticleNoiseUVModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleNoiseUVModule() = default;
		~ParticleNoiseUVModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		bool DrawImGui() override;

		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 揺れの強さ
		float strength_ = 0.1f;
		// ノイズの周波数
		float frequency_ = 1.0f;
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleNoiseUVModule, "NoiseUV");
} // Engine
