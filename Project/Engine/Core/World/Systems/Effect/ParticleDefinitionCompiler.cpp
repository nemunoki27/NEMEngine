#include "ParticleDefinitionCompiler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <stdexcept>

using namespace Engine;

namespace {

	void AppendExecution(std::vector<ParticleModuleExecutionGroup>& execution,
		ParticleModuleExecutionMode mode, IParticleModule* module) {

		if (mode == ParticleModuleExecutionMode::None) return;
		if (execution.empty() || execution.back().mode != mode) execution.push_back({mode, {}});
		execution.back().modules.push_back(module);
	}
}

std::shared_ptr<const ParticleSystemDefinition> ParticleDefinitionCompiler::Resolve(
	std::shared_ptr<const ParticleSystemSettings> settings) {

	if (!settings) throw std::invalid_argument("ParticleSystemの設定がありません");
	const auto found = definitions_.find(settings.get());
	if (found != definitions_.end()) {
		if (auto compiled = found->second.lock()) return compiled;
	}
	// 誰も参照しない編集世代をCacheへ残さない
	std::erase_if(definitions_, [](const auto& entry) { return entry.second.expired(); });
	auto compiled = Compile(std::move(settings));
	definitions_[compiled->settings.get()] = compiled;
	return compiled;
}

std::shared_ptr<const ParticleSystemDefinition> ParticleDefinitionCompiler::Compile(
	std::shared_ptr<const ParticleSystemSettings> settings) const {

	auto compiled = std::make_shared<ParticleSystemDefinition>();
	compiled->settings = std::move(settings);
	compiled->renderSettings = MakeParticleRenderSettings(compiled->settings->space, *compiled->settings);
	compiled->structure = static_cast<uint64_t>(compiled->settings->shape);
	Algorithm::HashCombine(compiled->structure, static_cast<uint64_t>(compiled->settings->space));
	Algorithm::HashCombine(compiled->structure, compiled->settings->emitter.maxParticles);
	const auto& trail = compiled->settings->trail;
	Algorithm::HashCombine(compiled->structure, trail.enabled);
	Algorithm::HashCombine(compiled->structure, static_cast<uint64_t>(trail.mode));
	Algorithm::HashCombine(compiled->structure, trail.maxPoints);
	Algorithm::HashCombine(compiled->structure, trail.maxTrails);
	Algorithm::HashCombine(compiled->structure, trail.memoryBudgetMB);
	Algorithm::HashCombine(compiled->structure, std::hash<AssetID>{}(compiled->settings->model));
	for (const auto& source : compiled->settings->phases) {

		ParticlePhaseDefinition phase;
		phase.lifetime = source.lifetime;
		phase.lifeEndMode = source.lifeEndMode;
		phase.parentSettings = source.parentSettings;
		Algorithm::HashCombine(compiled->structure, std::hash<Engine::UUID>{}(source.id));
		for (const auto& entry : source.modules) {

			auto& registry = ParticleModuleRegistry::GetInstance();
			auto module = registry.Create(registry.FindTypeID(entry.id));
			if (!module) throw std::invalid_argument("未登録のParticle Moduleです: " + entry.id);
			module->SetInstanceID(entry.instanceID);
			if (entry.id == "Rotation") module->SetStateSlot(compiled->stateCount++);
			module->FromJson(entry.params);
			const auto spawn = module->GetSpawnExecutionMode();
			const auto update = module->GetUpdateExecutionMode();
			AppendExecution(phase.spawnExecution, spawn, module.get());
			AppendExecution(phase.updateExecution, update, module.get());
			phase.hasSpawnBatch |= spawn == ParticleModuleExecutionMode::Batch;
			phase.hasUpdateBatch |= update == ParticleModuleExecutionMode::Batch;
			Algorithm::HashCombine(compiled->structure, std::hash<Engine::UUID>{}(entry.instanceID));
			Algorithm::HashCombine(compiled->structure, std::hash<std::string>{}(entry.id));
			if (entry.id == "Rotation") {
				// Moduleの状態配置が変わる方式の切替は再開する
				for (const char* key : {"mode", "speedMode", "valueType"})
					Algorithm::HashCombine(compiled->structure, std::hash<std::string>{}(entry.params.value(key, std::string{})));
			}
			phase.modules.push_back(std::move(module));
		}
		compiled->hasUpdateBatch |= phase.hasUpdateBatch;
		compiled->phases.push_back(std::move(phase));
	}
	if (compiled->phases.empty()) throw std::invalid_argument("ParticleSystemにはPhaseが必要です");
	return compiled;
}
