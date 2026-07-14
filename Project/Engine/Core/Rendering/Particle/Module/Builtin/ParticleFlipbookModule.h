#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	ParticleFlipbookModule class
	//	アトラステクスチャを寿命の進行度でコマ送りする
	//============================================================================
	class ParticleFlipbookModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleFlipbookModule() = default;
		~ParticleFlipbookModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		bool DrawImGui() override;

		ParticleModuleExecutionMode GetUpdateExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		void OnUpdate(Particle& particle, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// テクスチャの分割数
		// 行ごとの横タイル数
		std::vector<int32_t> tilesX_{ 1 };
		// 縦タイル数
		int32_t tilesY_ = 1;
		// 寿命内で何周させるか
		float cycles_ = 1.0f;
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleFlipbookModule, "Flipbook");
} // Engine
