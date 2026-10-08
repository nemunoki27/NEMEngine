#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleEffectDefinition.h"

namespace Engine::ParticleModuleExecution {

	// 寿命が尽きた粒子をLifeEndModeに従って遷移させる、破棄するならfalse
	bool AdvancePhaseOnLifeEnd(Particle& particle, const std::vector<ParticlePhaseDefinition>& phases);
	// Batchを含む更新計画をフェーズごとの連続範囲へ適用する
	void UpdatePhaseModules(std::vector<Particle>& particles, const ParticleGroupDefinition& group, float deltaTime);
	// 1粒子へ発生モジュールを適用する
	void ApplySpawnModules(Particle& particle, const ParticlePhaseDefinition& phase);
	// 1粒子へ更新モジュールを適用する
	void ApplyUpdateModules(Particle& particle, const ParticlePhaseDefinition& phase, float deltaTime);
	// 発生モジュールの実行計画を粒子範囲へ適用する
	void ExecuteSpawnModules(std::span<Particle> particles, const ParticlePhaseDefinition& phase);
	// 更新モジュールの実行計画を粒子範囲へ適用する
	void ExecuteUpdateModules(std::span<Particle> particles, const ParticlePhaseDefinition& phase, float deltaTime);
}
