#include "ParticleSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>
#include <Engine/Core/Rendering/Particle/ParticleEffectEditBridge.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#endif

// c++
#include <algorithm>

//============================================================================
//	ParticleSystem classMethods
//============================================================================
void Engine::ParticleSystem::Update(ECSWorld& world, SystemContext& context) {

	// ホットリロードの確認は間隔を空けて行う
	reloadCheckTimer_ += context.unscaledDeltaTime;
	const bool checkReload = kReloadCheckInterval <= reloadCheckTimer_;
	if (checkReload) {
		reloadCheckTimer_ = 0.0f;
	}

	world.ForEach<ParticleSystemComponent, ParticleSystemRuntimeComponent>(
		[&](const Entity& entity, ParticleSystemComponent& component,
			ParticleSystemRuntimeComponent&) {

			ParticleSystemRuntimeData* runtime =
				TryGetParticleSystemRuntime(world, entity);
			if (!runtime) {
				return;
			}
			const SceneObjectComponent* sceneObject =
				world.TryGetComponent<SceneObjectComponent>(entity);
			const bool active = !sceneObject || sceneObject->activeInHierarchy;
			const bool canPlay = active &&
				(context.mode == WorldMode::Play || component.playInEditMode);

			if (!runtime->initialized) {
				runtime->initialized = true;
				if (component.enabled && component.playOnAwake && canPlay) {
					StartEffect(*runtime, component.effect, false);
				}
			}
			ProcessCommands(component, *runtime);

			if (runtime->activeEffect != component.effect &&
				(runtime->playing || runtime->paused)) {
				StartEffect(*runtime, component.effect, runtime->effect.oneShot);
			}
			if (runtime->stopActionPending) {
				ApplyStopAction(world, entity, component, *runtime, context.mode);
			}
			if (!runtime->playing || runtime->paused) {
				return;
			}

			const float baseDeltaTime = component.useUnscaledTime ||
				context.mode != WorldMode::Play ? context.unscaledDeltaTime : context.deltaTime;
			const float deltaTime = baseDeltaTime * (std::max)(component.playbackSpeed, 0.0f);
			const bool updateSimulation = component.enabled && canPlay && 0.0f < deltaTime;
			const bool emissionEnabled = component.enabled &&
				!runtime->effect.emissionStopped;
			ResolvedWorldTransform emitterTransform{};
			const Matrix4x4 emitterWorld =
				TransformWorldUtility::ResolveWorldTransform(
					world, entity, emitterTransform) ?
				emitterTransform.matrix : Matrix4x4::Identity();

			ParticlePhaseParentSettings parentSettings{};
			bool useAssetParentSettings =
				component.simulationSpace == ParticleSystemSimulationSpace::EffectAsset;
			switch (component.simulationSpace) {
			case ParticleSystemSimulationSpace::Local:
				parentSettings.useEmitter = true;
				break;
			case ParticleSystemSimulationSpace::Custom:
				parentSettings.entityLocalFileID = component.customSimulationTarget;
				break;
			case ParticleSystemSimulationSpace::EffectAsset:
			case ParticleSystemSimulationSpace::World:
				break;
			}

			if (UpdateEffectInstance(world, runtime->effect, emitterWorld,
				parentSettings, useAssetParentSettings, context, deltaTime,
				updateSimulation, emissionEnabled, component.drawEmitterShape,
				checkReload)) {

				runtime->playing = false;
				runtime->paused = false;
				runtime->stopped = true;
				runtime->stopActionPending = true;
				ApplyStopAction(world, entity, component, *runtime, context.mode);
			}
		});
}

void Engine::ParticleSystem::ProcessCommands(
	const ParticleSystemComponent& component,
	ParticleSystemRuntimeData& runtime) const {

	std::vector<ParticleSystemCommand> commands = std::move(runtime.commands);
	runtime.commands.clear();
	for (const ParticleSystemCommand& command : commands) {

		switch (command.type) {
		case ParticleSystemCommandType::Play:
			if (runtime.paused && !runtime.stopped) {
				runtime.paused = false;
				runtime.playing = true;
			} else if (runtime.stopped) {
				StartEffect(runtime, component.effect, command.oneShot);
			}
			break;
		case ParticleSystemCommandType::Pause:
			if (runtime.playing && !runtime.stopped) {
				runtime.playing = false;
				runtime.paused = true;
			}
			break;
		case ParticleSystemCommandType::Stop:
			runtime.effect.emissionStopped = true;
			runtime.paused = false;
			runtime.stopped = true;
			if (command.stopBehavior ==
				ParticleSystemStopBehavior::StopEmittingAndClear) {

				ClearEffect(runtime);
				runtime.playing = false;
				runtime.stopped = true;
				runtime.stopActionPending = true;
			} else {
				runtime.playing = true;
			}
			break;
		case ParticleSystemCommandType::Clear:
			ClearEffect(runtime);
			break;
		case ParticleSystemCommandType::Restart:
			StartEffect(runtime, component.effect, command.oneShot);
			break;
		}
	}
}

void Engine::ParticleSystem::StartEffect(ParticleSystemRuntimeData& runtime,
	AssetID effectID, bool oneShot) const {

	runtime.effect = ParticleEffectInstanceRuntime{};
	runtime.effect.effect = effectID;
	runtime.effect.oneShot = oneShot;
	runtime.activeEffect = effectID;
	runtime.playing = true;
	runtime.paused = false;
	runtime.stopped = false;
	runtime.stopActionPending = false;
}

void Engine::ParticleSystem::ClearEffect(
	ParticleSystemRuntimeData& runtime) const {

	for (ParticleGroupRuntimeState& group : runtime.effect.runtimeGroups) {
		group.particles.clear();
		group.trails.clear();
	}
}

void Engine::ParticleSystem::ApplyStopAction(ECSWorld& world,
	const Entity& entity, ParticleSystemComponent& component,
	ParticleSystemRuntimeData& runtime, WorldMode mode) const {

	if (!runtime.stopActionPending) {
		return;
	}
	runtime.stopActionPending = false;
	if (mode != WorldMode::Play) {
		return;
	}
	switch (component.stopAction) {
	case ParticleSystemStopAction::Disable:
		world.GetCommandBuffer().EnqueueSetActiveSelfEnsuringComponent(entity, false);
		break;
	case ParticleSystemStopAction::Destroy:
		world.DestroyEntity(entity);
		break;
	case ParticleSystemStopAction::None:
		break;
	}
}

bool Engine::ParticleSystem::SynchronizeRuntimeGroups(
	ParticleEffectInstanceRuntime& instance, const EffectRuntime& effect) const {

	bool matched = instance.runtimeGroups.size() == effect.asset.groups.size();
	for (size_t i = 0; matched && i < effect.asset.groups.size(); ++i) {
		matched = instance.runtimeGroups[i].groupID == effect.asset.groups[i].id;
	}
	if (matched && instance.runtimeEffectRevision == effect.revision) {
		return false;
	}
	if (matched) {

		for (size_t i = 0; i < effect.asset.groups.size(); ++i) {
			instance.runtimeGroups[i].renderSettings =
				MakeParticleRenderSettings(effect.asset.space, effect.asset.groups[i]);
		}
		instance.runtimeEffectRevision = effect.revision;
		return true;
	}

	std::vector<ParticleGroupRuntimeState> previous = std::move(instance.runtimeGroups);
	instance.runtimeGroups.clear();
	instance.runtimeGroups.reserve(effect.asset.groups.size());
	for (const ParticleEffectGroup& group : effect.asset.groups) {

		auto it = std::find_if(previous.begin(), previous.end(), [&](const ParticleGroupRuntimeState& state) {
			return state.groupID == group.id;
			});
		if (it != previous.end()) {

			instance.runtimeGroups.emplace_back(std::move(*it));
			previous.erase(it);
		} else {
			ParticleGroupRuntimeState state{};
			state.groupID = group.id;
			instance.runtimeGroups.emplace_back(std::move(state));
		}
		instance.runtimeGroups.back().renderSettings = MakeParticleRenderSettings(effect.asset.space, group);
	}
	instance.runtimeEffectRevision = effect.revision;
	return true;
}

void Engine::ParticleSystem::RestartEffectInstance(
	ParticleEffectInstanceRuntime& instance, const ParticleEffectAsset& asset) const {

	instance.runtimeGroupEmissionMode = asset.groupEmission.mode;
	instance.runtimeGroupEmitTimer = (std::max)(0.0f, asset.groupEmission.interval);
	instance.runtimeGroupEmitted = false;
	for (size_t i = 0; i < instance.runtimeGroups.size(); ++i) {

		ParticleGroupRuntimeState& state = instance.runtimeGroups[i];
		state.emitTimer = i < asset.groups.size() ?
			(std::max)(0.0f, asset.groups[i].emitter.emitInterval) : 0.0f;
		state.emitted = false;
	}
}

bool Engine::ParticleSystem::UpdateGroupEmission(ParticleEffectInstanceRuntime& instance,
	const ParticleEffectAsset& asset, float deltaTime, bool emissionEnabled) const {

	if (!emissionEnabled) { return false; }

	if (asset.groupEmission.mode != ParticleEffectGroupEmissionMode::Simultaneous) {

		instance.runtimeGroupEmitTimer = 0.0f;
		instance.runtimeGroupEmitted = false;
		return false;
	}
	if (instance.oneShot && instance.runtimeGroupEmitted) {
		return false;
	}
	if (asset.groupEmission.waitForCompletion) {
		for (size_t i = 0; i < asset.groups.size(); ++i) {

			if (!asset.groups[i].enabled) { continue; }
			const ParticleGroupRuntimeState& state = instance.runtimeGroups[i];
			if (!state.particles.empty() || !state.trails.empty()) { return false; }
		}
	}

	instance.runtimeGroupEmitTimer += deltaTime;
	if (instance.runtimeGroupEmitTimer < (std::max)(0.0f, asset.groupEmission.interval)) {
		return false;
	}
	instance.runtimeGroupEmitTimer = 0.0f;
	instance.runtimeGroupEmitted = true;
	return true;
}

bool Engine::ParticleSystem::UpdateEffectInstance(ECSWorld& world,
	ParticleEffectInstanceRuntime& instance, const Matrix4x4& emitterWorld,
	const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings,
	SystemContext& context, float deltaTime, bool updateSimulation,
	bool emissionEnabled, bool drawEmitterShape, bool checkReload) {

	const AssetID effectID = instance.effect ? instance.effect : BuiltinAssets::Effects::DefaultParticle;
	if (instance.runtimeEffectID != effectID) {

		instance.runtimeEffectID = effectID;
		instance.runtimeEffectRevision = 0;
		instance.runtimeGroups.clear();
	}
	const EffectRuntime* effect = ResolveEffect(context, effectID, checkReload);
	if (!effect) {
		return true;
	}

	const ParticleEffectAsset& asset = effect->asset;
	const bool newRuntime = instance.runtimeGroups.empty();
	if (SynchronizeRuntimeGroups(instance, *effect)) {
		// グループ構成と描画設定を参照するRenderItemだけ再抽出する
		world.MarkRenderDataModified();
	}
	if (newRuntime) {
		RestartEffectInstance(instance, asset);
	} else if (instance.runtimeGroupEmissionMode != asset.groupEmission.mode) {

		RestartEffectInstance(instance, asset);
	}

	const bool simultaneousEmit = updateSimulation &&
		UpdateGroupEmission(instance, asset, deltaTime, emissionEnabled);
	for (size_t i = 0; i < asset.groups.size(); ++i) {

		ParticleGroupRuntimeState& state = instance.runtimeGroups[i];
		const ParticleEffectGroup& group = asset.groups[i];
		if (!group.enabled) {

			state.particles.clear();
			state.trails.clear();
			continue;
		}
		UpdateGroup(world, emitterWorld, state, asset, group, effect->groups[i],
			parentSettings, useAssetParentSettings,
			deltaTime, updateSimulation, simultaneousEmit, emissionEnabled,
			instance.oneShot, drawEmitterShape);
	}

	const bool hasRenderData = std::any_of(instance.runtimeGroups.begin(), instance.runtimeGroups.end(),
		[](const ParticleGroupRuntimeState& state) {
			return !state.particles.empty() || !state.trails.empty();
		});
	if (hasRenderData) { return false; }
	if (instance.emissionStopped) { return true; }
	if (asset.groupEmission.mode == ParticleEffectGroupEmissionMode::Simultaneous) {
		return instance.oneShot && instance.runtimeGroupEmitted;
	}
	for (size_t i = 0; i < asset.groups.size(); ++i) {

		const ParticleEffectGroup& group = asset.groups[i];
		if (!group.enabled) { continue; }
		if (!instance.oneShot && group.looping) { return false; }
		if (!instance.runtimeGroups[i].emitted) { return false; }
	}
	return true;
}

void Engine::ParticleSystem::UpdateGroup(ECSWorld& world, const Matrix4x4& emitterWorld,
	ParticleGroupRuntimeState& state, const ParticleEffectAsset& asset,
	const ParticleEffectGroup& group, const GroupRuntime& runtime,
	const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings,
	float deltaTime, bool updateSimulation, bool simultaneousEmit, bool emissionEnabled,
	bool oneShot, bool drawEmitterShape) {

	parentRuntimes_.assign(runtime.phases.size(), ParentRuntime{});
	ResolveParticleParents(world, emitterWorld, runtime,
		parentSettings, useAssetParentSettings, parentRuntimes_);
	const std::vector<ParentRuntime>& parents = parentRuntimes_;
	if (!updateSimulation) {

		UpdateParticleParents(state.particles, runtime,
			parentSettings, useAssetParentSettings, parents);
		if (drawEmitterShape) {
			DrawEmitterShape(emitterWorld, group.emitter, asset.space == PrimitiveRenderSpace::Screen2D);
		}
		return;
	}

	const bool emitOnce = oneShot ||
		(asset.groupEmission.mode == ParticleEffectGroupEmissionMode::Independent && !group.looping);
	bool emitAllowed = emissionEnabled &&
		(asset.groupEmission.mode == ParticleEffectGroupEmissionMode::Simultaneous ? simultaneousEmit : true);
	if (emitOnce && state.emitted) { emitAllowed = false; }

	// 寿命と移動、終端はLifeEndModeに従って遷移し、破棄する粒子は末尾と入れ替える
	std::vector<Particle>& particles = state.particles;
	const bool hasUpdateBatch = runtime.hasUpdateBatch;
	for (size_t i = 0; i < particles.size();) {

		Particle& particle = particles[i];
		if (particle.phaseIndex < runtime.phases.size()) {
			const PhaseRuntime& phase = runtime.phases[particle.phaseIndex];
			UpdateParticleParent(particle, ResolveParticleParentSettings(
				phase, parentSettings, useAssetParentSettings), parents[particle.phaseIndex]);
		}
		const uint32_t previousPhase = particle.phaseIndex;
		particle.previousAge = particle.age;
		particle.previousPhaseIndex = particle.phaseIndex;
		particle.age += deltaTime;
		if (particle.lifetime <= particle.age && !AdvancePhaseOnLifeEnd(particle, runtime.phases)) {

			if (group.trail.enabled && group.trail.keepAfterParticleDeath) {

				auto trailIt = state.trails.find(particle.id);
				if (trailIt != state.trails.end()) {

					ParticleTrailRuntime& trailRuntime = trailIt->second;
					trailRuntime.owner = particle;
					trailRuntime.hasOwner = true;
					trailRuntime.detached = true;
					trailRuntime.detachedThisFrame = true;
				}
			}

			particle = particles.back();
			particles.pop_back();
			continue;
		}
		if (particle.phaseIndex != previousPhase && particle.phaseIndex < runtime.phases.size()) {
			const PhaseRuntime& phase = runtime.phases[particle.phaseIndex];
			UpdateParticleParent(particle, ResolveParticleParentSettings(
				phase, parentSettings, useAssetParentSettings), parents[particle.phaseIndex]);
		}
		particle.pos += particle.velocity * deltaTime;
		if (!hasUpdateBatch) {

			if (particle.phaseIndex < runtime.phases.size()) {
				ApplyUpdateModules(particle, runtime.phases[particle.phaseIndex], deltaTime);
			}
			const ParentRuntime* parent = particle.phaseIndex < parents.size() ?
				&parents[particle.phaseIndex] : nullptr;
			RefreshParticleWorldTransform(particle, parent);
		}
		++i;
	}

	// Batchを含む場合だけフェーズ別の連続範囲を作って登録順に実行する
	if (hasUpdateBatch) {

		UpdatePhaseModules(particles, runtime, deltaTime);
		for (Particle& particle : particles) {
			const ParentRuntime* parent = particle.phaseIndex < parents.size() ?
				&parents[particle.phaseIndex] : nullptr;
			RefreshParticleWorldTransform(particle, parent);
		}
	}

	// 発生間隔ごとに発生させ、上限でクランプする
	if (emitAllowed) {

		const ParticleEmitterSettings& emitterSettings = group.emitter;
		uint32_t spawnCount = 0;
		if (asset.groupEmission.mode == ParticleEffectGroupEmissionMode::Simultaneous) {
			spawnCount = emitterSettings.emitCount.Sample();
			state.emitted = true;
		} else if (emitOnce) {

			spawnCount = emitterSettings.emitCount.Sample();
			state.emitted = true;
		} else {

			state.emitTimer += deltaTime;
			if (emitterSettings.emitInterval <= state.emitTimer) {

				state.emitTimer = 0.0f;
				spawnCount = emitterSettings.emitCount.Sample();
			}
		}
		const uint32_t capacity = static_cast<uint32_t>(
			(std::max)(0, static_cast<int32_t>(emitterSettings.maxParticles) - static_cast<int32_t>(particles.size())));
		spawnCount = (std::min)(spawnCount, capacity);
		if (0 < spawnCount && !runtime.phases.empty()) {

			particles.resize(particles.size() + spawnCount);
			std::span<Particle> newborn(particles.data() + particles.size() - spawnCount, spawnCount);
			// エミッター形状から初期状態を決めてから、先頭フェーズのモジュールの発生処理を通す
			const PhaseRuntime& firstPhase = runtime.phases.front();
			InitEmitterParticles(newborn, emitterSettings, firstPhase.lifetime,
				asset.space == PrimitiveRenderSpace::Screen2D, state.nextParticleID);
			state.nextParticleID += spawnCount;

			// エミッターのワールド行列で発生位置と速度を変換する
			const bool hasSpawnBatch = firstPhase.hasSpawnBatch || firstPhase.hasUpdateBatch;
			if (hasSpawnBatch) {

				ExecuteSpawnModules(newborn, firstPhase);
			}
			for (Particle& particle : newborn) {

				if (!hasSpawnBatch) { ApplySpawnModules(particle, firstPhase); }
				const Vector3 worldPos = Vector3::Transform(particle.pos, emitterWorld);
				particle.velocity = Vector3::Transform(particle.pos + particle.velocity, emitterWorld) - worldPos;
				particle.spawnDirection = Vector3::NormalizeOr(
					Vector3::Transform(particle.pos + particle.spawnDirection, emitterWorld) - worldPos,
					Vector3(0.0f, 1.0f, 0.0f));
				particle.pos = worldPos;
				// 発生時の回転とスケールは親ローカル値として継承する
				UpdateParticleParent(particle, ResolveParticleParentSettings(
					firstPhase, parentSettings, useAssetParentSettings), parents.front(), false);
				if (!hasSpawnBatch) {

					// 発生した瞬間の見た目を確定させる
					ApplyUpdateModules(particle, firstPhase, 0.0f);
					RefreshParticleWorldTransform(particle, &parents.front());
				}
			}
			if (hasSpawnBatch) {

				ExecuteUpdateModules(newborn, firstPhase, 0.0f);
				for (Particle& particle : newborn) {
					RefreshParticleWorldTransform(particle, &parents.front());
				}
			}
		}
	}
	UpdateDetachedTrailOwners(state, runtime, parentSettings,
		useAssetParentSettings, parents, group.trail, deltaTime);

	// トレイルの軌跡点をワールド空間で記録する
	if (group.trail.enabled) {
		RecordTrails(state, group.trail, deltaTime);
	} else if (!state.trails.empty()) {
		state.trails.clear();
	}

	// エミッター形状のデバッグ描画
	if (drawEmitterShape) {
		DrawEmitterShape(emitterWorld, group.emitter, asset.space == PrimitiveRenderSpace::Screen2D);
	}
}

const Engine::ParticleSystem::EffectRuntime* Engine::ParticleSystem::ResolveEffect(
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

Engine::ParticleSystem::EffectRuntime Engine::ParticleSystem::LoadEffect(
	SystemContext& context, AssetID effectID) const {

	// アセットを読み込みフェーズを構築する、未登録のモジュールは読み飛ばす
	EffectRuntime runtime{};
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

bool Engine::ParticleSystem::AdvancePhaseOnLifeEnd(Particle& particle, const std::vector<PhaseRuntime>& phases) const {

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

void Engine::ParticleSystem::UpdatePhaseModules(std::vector<Particle>& particles,
	const GroupRuntime& group, float deltaTime) const {

	// Batchモジュール用にフェーズ順の連続範囲を作る
	std::sort(particles.begin(), particles.end(),
		[](const Particle& lhs, const Particle& rhs) { return lhs.phaseIndex < rhs.phaseIndex; });
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

void Engine::ParticleSystem::ApplySpawnModules(Particle& particle, const PhaseRuntime& phase) const {

	for (const ModuleExecutionGroup& group : phase.spawnExecution) {

		if (group.mode != ParticleModuleExecutionMode::PerParticle) {
			continue;
		}
		for (IParticleModule* module : group.modules) {
			module->OnSpawn(particle);
		}
	}
}

void Engine::ParticleSystem::ApplyUpdateModules(
	Particle& particle, const PhaseRuntime& phase, float deltaTime) const {

	for (const ModuleExecutionGroup& group : phase.updateExecution) {

		if (group.mode != ParticleModuleExecutionMode::PerParticle) {
			continue;
		}
		for (IParticleModule* module : group.modules) {
			module->OnUpdate(particle, deltaTime);
		}
	}
}

void Engine::ParticleSystem::ExecuteSpawnModules(
	std::span<Particle> particles, const PhaseRuntime& phase) const {

	for (const ModuleExecutionGroup& group : phase.spawnExecution) {

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

void Engine::ParticleSystem::ExecuteUpdateModules(
	std::span<Particle> particles, const PhaseRuntime& phase, float deltaTime) const {

	for (const ModuleExecutionGroup& group : phase.updateExecution) {

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

void Engine::ParticleSystem::InitEmitterParticles(std::span<Particle> newborn,
	const ParticleEmitterSettings& settings, const ParticleValue<float>& lifetime,
	bool is2D, uint32_t firstSpawnIndex) const {

	const IParticleEmitterShape* shape = ParticleEmitterShapeRegistry::GetInstance().Find(settings.shape);

	ParticleSpawnIndex spawnIndex{};
	spawnIndex.global = firstSpawnIndex;
	spawnIndex.batchCount = static_cast<uint32_t>(newborn.size());
	for (Particle& particle : newborn) {

		Vector3 position = Vector3::AnyInit(0.0f);
		Vector3 direction = Vector3(0.0f, 1.0f, 0.0f);
		if (shape) {
			shape->InitParticle(position, direction, settings, is2D, spawnIndex);
		}
		++spawnIndex.global;
		++spawnIndex.batchIndex;

		// 発生座標にオフセットを掛ける
		particle.pos = position + settings.emitOffset.Sample();
		particle.spawnDirection = Vector3::NormalizeOr(direction, Vector3(0.0f, 1.0f, 0.0f));
		particle.velocity = direction * settings.speed.Sample();
		particle.lifetime = (std::max)(lifetime.Sample(), 0.001f);
		particle.id = spawnIndex.global;
	}
}

void Engine::ParticleSystem::DrawEmitterShape(const Matrix4x4& emitterWorld,
	const ParticleEmitterSettings& settings, bool is2D) const {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

	const IParticleEmitterShape* shape = ParticleEmitterShapeRegistry::GetInstance().Find(settings.shape);
	if (!shape) {
		return;
	}

	// エミッターのワールド位置と回転を取り出す
	Vector3 center = Vector3::AnyInit(0.0f);
	Quaternion rotation = Quaternion::Identity();
	Vector3 scale{};
	DecomposeAffine3D(emitterWorld, center, rotation, scale);
	LineRenderer3D* renderer = nullptr;
	if (!is2D) {
		renderer = LineRenderer::GetInstance()->Get3D();
		if (renderer) {
			renderer->SetOccludedMode(true);
		}
	}
	shape->DrawShape(settings, center, rotation, is2D);
	if (renderer) {
		renderer->SetOccludedMode(false);
	}
#endif
}
