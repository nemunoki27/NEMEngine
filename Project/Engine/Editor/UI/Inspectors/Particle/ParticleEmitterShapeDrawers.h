#pragma once

#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>

namespace Engine::ParticleEmitterShapeDrawers {

	// 発生形状の設定を編集する
	bool DrawSphere(ParticleEmitterSettings& settings);
	// 発生形状の設定を編集する
	bool DrawHemisphere(ParticleEmitterSettings& settings);
	// 発生形状の設定を編集する
	bool DrawBox(ParticleEmitterSettings& settings);
	// 発生形状の設定を編集する
	bool DrawTorus(ParticleEmitterSettings& settings);
	// 発生形状の設定を編集する
	bool DrawCircle(ParticleEmitterSettings& settings);
	// 発生形状の設定を編集する
	bool DrawCone(ParticleEmitterSettings& settings);
	// 発生形状の設定を編集する
	bool DrawPoint(ParticleEmitterSettings& settings);
	// 発生形状の設定を編集する
	bool DrawRect(ParticleEmitterSettings& settings);
	// 発生形状の設定を編集する
	bool DrawCone2D(ParticleEmitterSettings& settings);
}
