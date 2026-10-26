#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/IParticleModule.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleCustomDataSettings.h>

namespace Engine {

	// 粒子固有の任意数値を描画へ公開する
	class ParticleCustomDataModule : public IParticleModule {
	public:
		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		const ParticleCustomDataSettings& GetSettings() const { return settings_; }
		void SetSettings(const ParticleCustomDataSettings& settings) { settings_ = settings; }
	private:
		ParticleCustomDataSettings settings_{};
	};
}
