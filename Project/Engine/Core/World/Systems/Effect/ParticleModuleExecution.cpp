#include "ParticleModuleExecution.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>

namespace {

	// 色Moduleの更新が終わってから発光色を揃える
	void ApplyCurrentEmissiveColor(Engine::Particle& particle) {

		particle.emissive.x = particle.color.r;
		particle.emissive.y = particle.color.g;
		particle.emissive.z = particle.color.b;
	}
}

bool Engine::ParticleModuleExecution::AdvancePhaseOnLifeEnd(Particle& particle, const std::vector<ParticlePhaseDefinition>& phases) {

	if (phases.size() <= particle.phaseIndex) {
		return false;
	}

	// 寿命が尽きたときの挙動、値の閉じたenumなのでここで一括処理する
	switch (phases[particle.phaseIndex].lifeEndMode) {
	case ParticleLifeEndMode::Advance: {

		// 次フェーズへ遷移する、位置や速度や色は現在値を引き継ぐ
		const uint32_t next = particle.phaseIndex + 1;
		if (phases.size() <= next) {
			return false;
		}
		particle.phaseIndex = next;
		particle.age = 0.0f;
		particle.lifetime = (std::max)(phases[next].lifetime.Sample(), 0.001f);
		return true;
	}
	case ParticleLifeEndMode::Clamp:

		// 進行度1.0の見た目を保持して生存し続ける
		particle.age = particle.lifetime;
		return true;
	case ParticleLifeEndMode::Reset:

		// 同フェーズを最初からやり直す
		particle.age = 0.0f;
		return true;
	case ParticleLifeEndMode::Kill:
	default:
		return false;
	}
}

void Engine::ParticleModuleExecution::ApplySpawnModules(Particle& particle, const ParticlePhaseDefinition& phase) {

	for (const ParticleModuleExecutionGroup& group : phase.spawnExecution) {

		if (group.mode != ParticleModuleExecutionMode::PerParticle) {
			continue;
		}
		for (IParticleModule* module : group.modules) {
			module->OnSpawn(particle);
		}
	}
}

void Engine::ParticleModuleExecution::ApplyUpdateModules(
	Particle& particle, const ParticlePhaseDefinition& phase, float deltaTime) {

	for (const ParticleModuleExecutionGroup& group : phase.updateExecution) {

		if (group.mode != ParticleModuleExecutionMode::PerParticle) {
			continue;
		}
		for (IParticleModule* module : group.modules) {
			module->OnUpdate(particle, deltaTime);
		}
	}
	if (phase.useParticleColorForEmissive) ApplyCurrentEmissiveColor(particle);
}

void Engine::ParticleModuleExecution::ExecuteSpawnModules(
	std::span<Particle> particles, const ParticlePhaseDefinition& phase) {

	for (const ParticleModuleExecutionGroup& group : phase.spawnExecution) {

		if (group.mode == ParticleModuleExecutionMode::PerParticle) {

			for (Particle& particle : particles) {
				for (IParticleModule* module : group.modules) {
					module->OnSpawn(particle);
				}
			}
			continue;
		}
		for (IParticleModule* module : group.modules) {
			module->OnSpawnBatch(particles);
		}
	}
}

void Engine::ParticleModuleExecution::ExecuteUpdateModules(
	std::span<Particle> particles, const ParticlePhaseDefinition& phase, float deltaTime) {

	for (const ParticleModuleExecutionGroup& group : phase.updateExecution) {

		if (group.mode == ParticleModuleExecutionMode::PerParticle) {

			for (Particle& particle : particles) {
				for (IParticleModule* module : group.modules) {
					module->OnUpdate(particle, deltaTime);
				}
			}
			continue;
		}
		for (IParticleModule* module : group.modules) {
			module->OnUpdateBatch(particles, deltaTime);
		}
	}
	if (phase.useParticleColorForEmissive) {
		for (Particle& particle : particles) ApplyCurrentEmissiveColor(particle);
	}
}
