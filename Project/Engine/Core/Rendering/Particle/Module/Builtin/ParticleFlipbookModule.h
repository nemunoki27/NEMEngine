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

		// 保存と実行に使う設定
		struct Settings {

			// テクスチャの分割数
			// 行ごとの横タイル数
			std::vector<int32_t> tilesX{ 1 };
			// 縦タイル数
			int32_t tilesY = 1;
			// 寿命内で何周させるか
			float cycles = 1.0f;
		};

		//========================================================================
		//	public Methods
		//========================================================================

		ParticleFlipbookModule() = default;
		~ParticleFlipbookModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;

		ParticleModuleExecutionMode GetUpdateExecutionMode() const override { return ParticleModuleExecutionMode::PerParticle; }
		void OnUpdate(Particle& particle, float deltaTime) override;

		//--------- accessor -----------------------------------------------------

		const Settings& GetSettings() const { return settings_; }
		void SetSettings(const Settings& settings) { settings_ = settings; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Settings settings_{};

	};

} // Engine
