#include "ParticleSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>

// c++
#include <utility>

//============================================================================
//	ParticleSystem runtime methods
//============================================================================
void Engine::ParticleSystem::BuildGroups(EffectRuntime& runtime) const {

	runtime.groups.clear();
	runtime.groups.reserve(runtime.asset.groups.size());
	for (const ParticleEffectGroup& groupDef : runtime.asset.groups) {

		GroupRuntime group{};
		group.id = groupDef.id;
		group.phases.reserve(groupDef.phases.size());
		for (const ParticleEffectPhase& phaseDef : groupDef.phases) {

			PhaseRuntime phase{};
			phase.lifetime = phaseDef.lifetime;
			phase.lifeEndMode = phaseDef.lifeEndMode;
			phase.parentSettings = phaseDef.parentSettings;
			auto appendExecution = [](std::vector<ModuleExecutionGroup>& execution,
				ParticleModuleExecutionMode mode, IParticleModule* module) {

				if (mode == ParticleModuleExecutionMode::None) { return; }
				if (execution.empty() || execution.back().mode != mode) {

					ModuleExecutionGroup executionGroup{};
					executionGroup.mode = mode;
					execution.emplace_back(std::move(executionGroup));
				}
				execution.back().modules.emplace_back(module);
			};
			for (const ParticleEffectModuleEntry& entry : phaseDef.modules) {

				ParticleModuleRegistry& registry = ParticleModuleRegistry::GetInstance();
				const ParticleModuleRegistry::TypeID typeID = registry.FindTypeID(entry.id);
				auto module = registry.Create(typeID);
				if (!module) { continue; }
				module->FromJson(entry.params);
				IParticleModule* modulePtr = module.get();
				const ParticleModuleExecutionMode spawnMode = module->GetSpawnExecutionMode();
				const ParticleModuleExecutionMode updateMode = module->GetUpdateExecutionMode();
				appendExecution(phase.spawnExecution, spawnMode, modulePtr);
				appendExecution(phase.updateExecution, updateMode, modulePtr);
				phase.hasSpawnBatch |= spawnMode == ParticleModuleExecutionMode::Batch;
				phase.hasUpdateBatch |= updateMode == ParticleModuleExecutionMode::Batch;
				phase.modules.emplace_back(std::move(module));
			}
			group.hasUpdateBatch |= phase.hasUpdateBatch;
			group.phases.emplace_back(std::move(phase));
		}
		runtime.groups.emplace_back(std::move(group));
	}
}
