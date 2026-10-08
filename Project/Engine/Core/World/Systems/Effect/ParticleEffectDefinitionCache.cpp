#include "ParticleEffectDefinitionCache.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/Particle/ParticleEffectEditBridge.h>
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <exception>

const Engine::ParticleEffectDefinition* Engine::ParticleEffectDefinitionCache::ResolveEffect(
	SystemContext& context, AssetID effectID, bool checkReload) {

	if (!context.assetDatabase) return nullptr;
	if (!effectID) effectID = BuiltinAssets::Effects::DefaultParticle;
	ParticleEffectDefinition& current = effectCache_[effectID];
	ParticleEffectEditBridge& bridge = ParticleEffectEditBridge::GetInstance();
	const uint64_t editRevision = bridge.GetRevision(effectID);
	const uint64_t contentRevision = context.assetDatabase->GetContentRevision(effectID);
	const auto path = context.assetDatabase->ResolveFullPath(effectID);
	auto writeTime = current.lastWriteTime;
	if (checkReload || !current.attempted || current.path != path) {
		std::error_code error;
		writeTime = std::filesystem::last_write_time(path, error);
		if (error) writeTime = {};
	}
	const bool reload = !current.attempted || current.appliedEditVersion != editRevision ||
		current.contentRevision != contentRevision || current.path != path || current.lastWriteTime != writeTime;
	if (!reload) return current.valid ? &current : nullptr;

	// 失敗した入力は更新されるまで再試行しない
	current.attempted = true;
	current.appliedEditVersion = editRevision;
	current.contentRevision = contentRevision;
	current.path = path;
	current.lastWriteTime = writeTime;
	try {
		ParticleEffectDefinition replacement{};
		uint64_t consumedVersion = 0;
		if (bridge.TryConsume(effectID, consumedVersion, replacement.asset)) {
			BuildGroups(replacement);
			replacement.valid = true;
		} else {
			replacement = LoadEffect(context, effectID);
		}
		if (replacement.valid) {
			// 完成した定義だけを公開し、失敗時は現在の粒子を維持する
			replacement.revision = current.revision + 1;
			replacement.appliedEditVersion = editRevision;
			replacement.contentRevision = contentRevision;
			replacement.path = path;
			replacement.lastWriteTime = writeTime;
			replacement.attempted = true;
			current = std::move(replacement);
		} else {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"Effectの再読込に失敗しました path={}", path.string());
		}
	} catch (const std::exception& error) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"Effectの再読込に失敗しました path={} 内容={}", path.string(), error.what());
		return current.valid ? &current : nullptr;
	}
	return current.valid ? &current : nullptr;
}
Engine::ParticleEffectDefinition Engine::ParticleEffectDefinitionCache::LoadEffect(
	SystemContext& context, AssetID effectID) const {

	// アセットを読み込みフェーズを構築する、未登録のモジュールは読み飛ばす
	ParticleEffectDefinition runtime{};
	runtime.path = context.assetDatabase->ResolveFullPath(effectID);
	if (runtime.path.empty()) {
		return runtime;
	}

	std::error_code ec;
	runtime.lastWriteTime = std::filesystem::last_write_time(runtime.path, ec);

	const nlohmann::json data = JsonAdapter::Load(runtime.path, false);
	if (!FromJson(data, runtime.asset)) {
		return runtime;
	}

	runtime.valid = true;
	BuildGroups(runtime);
	return runtime;
}

void Engine::ParticleEffectDefinitionCache::BuildGroups(ParticleEffectDefinition& runtime) const {

	runtime.groups.clear();
	runtime.groups.reserve(runtime.asset.groups.size());
	for (const ParticleEffectGroup& groupDef : runtime.asset.groups) {

		ParticleGroupDefinition group{};
		group.id = groupDef.id;
		group.phases.reserve(groupDef.phases.size());
		for (const ParticleEffectPhase& phaseDef : groupDef.phases) {

			ParticlePhaseDefinition phase{};
			phase.lifetime = phaseDef.lifetime;
			phase.lifeEndMode = phaseDef.lifeEndMode;
			phase.parentSettings = phaseDef.parentSettings;
			auto appendExecution = [](std::vector<ParticleModuleExecutionGroup>& execution,
				ParticleModuleExecutionMode mode, IParticleModule* module) {

				if (mode == ParticleModuleExecutionMode::None) { return; }
				if (execution.empty() || execution.back().mode != mode) {

					ParticleModuleExecutionGroup executionGroup{};
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
				module->SetInstanceID(entry.instanceID);
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
