#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/IParticleModule.h>
#include <Engine/Core/Rendering/Particle/ParticleMotionSettings.h>

namespace Engine {

	// Scene入力はGPUのWorldSnapshotで解決する
	template <typename Settings>
	class ParticleWorldModule : public IParticleModule {
	public:

		void FromJson(const nlohmann::json& params) override { from_json(params, settings_); }
		nlohmann::json ToJson() const override { nlohmann::json out; to_json(out, settings_); return out; }
		const Settings& GetSettings() const { return settings_; }
		void SetSettings(const Settings& settings) { settings_ = settings; }
	private:

		Settings settings_{};
	};

	using ParticleForceFieldModule = ParticleWorldModule<ParticleForceFieldSettings>;
	using ParticleVectorFieldModule = ParticleWorldModule<ParticleVectorFieldSettings>;
	using ParticleSplineMovementModule = ParticleWorldModule<ParticleSplineMovementSettings>;
	using ParticleCollisionModule = ParticleWorldModule<ParticleCollisionSettings>;
}
