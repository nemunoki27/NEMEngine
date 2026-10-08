#include "ParticleModuleExecution.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>

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

void Engine::ParticleModuleExecution::UpdatePhaseModules(std::vector<Particle>& particles,
	const ParticleGroupDefinition& group, float deltaTime) {

	// Batchモジュール用にフェーズ順の連続範囲を作る
	std::sort(particles.begin(), particles.end(), [](const Particle& lhs, const Particle& rhs) {		 return lhs.phaseIndex < rhs.phaseIndex; });
	size_t begin = 0;
	while (begin < particles.size()) {

		const uint32_t phaseIndex = particles[begin].phaseIndex;
		size_t end = begin;
		while (end < particles.size() && particles[end].phaseIndex == phaseIndex) {
			++end;
		}
		if (phaseIndex < group.phases.size()) {

			std::span<Particle> range(particles.data() + begin, end - begin);
			ExecuteUpdateModules(range, group.phases[phaseIndex], deltaTime);
		}
		begin = end;
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
}
