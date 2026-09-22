#include "ParticleEffectDefinitionCache.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/Particle/ParticleEffectEditBridge.h>
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>

const Engine::ParticleEffectDefinition* Engine::ParticleEffectDefinitionCache::ResolveEffect(
	SystemContext& context, AssetID effectID, bool checkReload) {

	if (!context.assetDatabase) {
		return nullptr;
	}
	// 空のエフェクトはビルトインの既定エフェクトへ解決する
	if (!effectID) {
		effectID = BuiltinAssets::Effects::DefaultParticle;
	}

	// キャッシュ済みならエディター編集とファイル更新を確認してそのまま返す
	auto found = effectCache_.find(effectID);
	if (found != effectCache_.end()) {

		// エディターの編集内容は保存を待たず即反映する
		ParticleEffectAsset editedAsset{};
		if (ParticleEffectEditBridge::GetInstance().TryConsume(
			effectID, found->second.appliedEditVersion, editedAsset)) {

			found->second.asset = std::move(editedAsset);
			found->second.valid = true;
			BuildGroups(found->second);
			++found->second.revision;
		} else if (checkReload && !found->second.path.empty()) {

			std::error_code ec;
			const auto lastWriteTime = std::filesystem::last_write_time(found->second.path, ec);
			if (!ec && found->second.lastWriteTime != lastWriteTime) {

				const uint64_t appliedEditVersion = found->second.appliedEditVersion;
				const uint64_t revision = found->second.revision;
				found->second = LoadEffect(context, effectID);
				found->second.appliedEditVersion = appliedEditVersion;
				found->second.revision = revision + 1;
			}
		}
		return found->second.valid ? &found->second : nullptr;
	}

	auto [it, inserted] = effectCache_.emplace(effectID, LoadEffect(context, effectID));
	return it->second.valid ? &it->second : nullptr;
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
