#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>

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

		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// アトラスの分割数
		int32_t tilesX_ = 1;
		int32_t tilesY_ = 1;
		// 寿命内で何周させるか
		float cycles_ = 1.0f;
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleFlipbookModule, "Flipbook");
} // Engine
