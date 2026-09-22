#include "ParticleInstanceUpdater.h"

//============================================================================
//	include
//============================================================================
#include "ParticleParenting.h"
#include "ParticleModuleExecution.h"
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

using namespace Engine::ParticleParenting;
using namespace Engine::ParticleModuleExecution;

bool Engine::ParticleInstanceUpdater::SynchronizeRuntimeGroups(
	ParticleEffectInstanceRuntime& instance, const ParticleEffectDefinition& effect) const {

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

void Engine::ParticleInstanceUpdater::RestartEffectInstance(
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

bool Engine::ParticleInstanceUpdater::UpdateGroupEmission(ParticleEffectInstanceRuntime& instance,
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

bool Engine::ParticleInstanceUpdater::UpdateEffectInstance(ECSWorld& world,
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
	const ParticleEffectDefinition* effect = definitions_.ResolveEffect(context, effectID, checkReload);
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

void Engine::ParticleInstanceUpdater::UpdateGroup(ECSWorld& world, const Matrix4x4& emitterWorld,
	ParticleGroupRuntimeState& state, const ParticleEffectAsset& asset,
	const ParticleEffectGroup& group, const ParticleGroupDefinition& runtime,
	const ParticlePhaseParentSettings& parentSettings, bool useAssetParentSettings,
	float deltaTime, bool updateSimulation, bool simultaneousEmit, bool emissionEnabled,
	bool oneShot, bool drawEmitterShape) {

	parentRuntimes_.assign(runtime.phases.size(), ParticleParentPose{});
	ResolveParticleParents(world, emitterWorld, runtime,
		parentSettings, useAssetParentSettings, parentRuntimes_);
	const std::vector<ParticleParentPose>& parents = parentRuntimes_;
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
			const ParticlePhaseDefinition& phase = runtime.phases[particle.phaseIndex];
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
			const ParticlePhaseDefinition& phase = runtime.phases[particle.phaseIndex];
			UpdateParticleParent(particle, ResolveParticleParentSettings(
				phase, parentSettings, useAssetParentSettings), parents[particle.phaseIndex]);
		}
		particle.pos += particle.velocity * deltaTime;
		if (!hasUpdateBatch) {

			if (particle.phaseIndex < runtime.phases.size()) {
				ApplyUpdateModules(particle, runtime.phases[particle.phaseIndex], deltaTime);
			}
			const ParticleParentPose* parent = particle.phaseIndex < parents.size() ?
				&parents[particle.phaseIndex] : nullptr;
			RefreshParticleWorldTransform(particle, parent);
		}
		++i;
	}

	// Batchを含む場合だけフェーズ別の連続範囲を作って登録順に実行する
	if (hasUpdateBatch) {

		UpdatePhaseModules(particles, runtime, deltaTime);
		for (Particle& particle : particles) {
			const ParticleParentPose* parent = particle.phaseIndex < parents.size() ?
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
			const ParticlePhaseDefinition& firstPhase = runtime.phases.front();
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
	trails_.UpdateDetachedTrailOwners(state, runtime, parentSettings,
		useAssetParentSettings, parents, group.trail, deltaTime);

	// トレイルの軌跡点をワールド空間で記録する
	if (group.trail.enabled) {
		trails_.RecordTrails(state, group.trail, deltaTime);
	} else if (!state.trails.empty()) {
		state.trails.clear();
	}

	// エミッター形状のデバッグ描画
	if (drawEmitterShape) {
		DrawEmitterShape(emitterWorld, group.emitter, asset.space == PrimitiveRenderSpace::Screen2D);
	}
}

void Engine::ParticleInstanceUpdater::InitEmitterParticles(std::span<Particle> newborn,
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

void Engine::ParticleInstanceUpdater::DrawEmitterShape(const Matrix4x4& emitterWorld,
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
