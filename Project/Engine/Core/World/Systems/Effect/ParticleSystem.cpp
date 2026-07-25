#include "ParticleSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/EffectEmitterComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/ParticleEffectEditBridge.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#endif

// 更新モジュール
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleSizeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleRotationModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleGravityForceModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleNoiseForceModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleSpiralMovementModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticlePendulumMovementModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleFlipbookModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleShapeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleCustomShaderParameterModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleScaleOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorUVModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleNoiseUVModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleEmissiveModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleAlphaReferenceModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleTrailSizeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleTrailColorOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleTrailColorUVModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleTrailCustomShaderParameterModule.h>
// 形状別
#include <Engine/Core/Rendering/Particle/Parametric/ParticleRingParametricShape.h>
#include <Engine/Core/Rendering/Particle/Parametric/ParticleCylinderParametricShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleSphereEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleHemisphereEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleBoxEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleTorusEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleCircleEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleConeEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticlePointEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleRectEmitterShape.h>
#include <Engine/Core/Rendering/Particle/Emitter/Shapes/ParticleCone2DEmitterShape.h>

// c++
#include <cmath>

//============================================================================
//	ParticleSystem internal
//============================================================================
namespace {

	// 親行列を回転とスケールへ分解する
	bool DecomposeParentMatrix(const Engine::Matrix4x4& matrix,
		Engine::Quaternion& outRotation, Engine::Vector3& outScale) {

		Engine::Vector3 translation{};
		return Engine::DecomposeAffine3D(matrix, translation, outRotation, outScale);
	}

	// 0スケールを避けてワールドスケールを親ローカルへ変換する
	Engine::Vector3 DivideScale(const Engine::Vector3& value, const Engine::Vector3& divisor) {

		constexpr float kMinScale = 1.0e-6f;
		return Engine::Vector3(
			std::abs(divisor.x) <= kMinScale ? value.x : value.x / divisor.x,
			std::abs(divisor.y) <= kMinScale ? value.y : value.y / divisor.y,
			std::abs(divisor.z) <= kMinScale ? value.z : value.z / divisor.z);
	}

	// 親ローカル姿勢をワールド姿勢へ焼き込んで親を解除する
	void BakeParticleParentToWorld(Engine::Particle& particle) {

		if (!particle.hasParent) {
			return;
		}

		particle.pos = Engine::Vector3::Transform(particle.pos, particle.parentMatrix);
		particle.velocity = Engine::Vector3::TransferNormal(particle.velocity, particle.parentMatrix);
		particle.spawnDirection = Engine::Vector3::NormalizeOr(
			Engine::Vector3::TransferNormal(particle.spawnDirection, particle.parentMatrix),
			Engine::Vector3(0.0f, 1.0f, 0.0f));

		Engine::Quaternion parentRotation{};
		Engine::Vector3 parentScale{};
		if (DecomposeParentMatrix(particle.parentMatrix, parentRotation, parentScale)) {
			particle.rotation = Engine::Quaternion::Normalize(parentRotation * particle.rotation);
			particle.scale = parentScale * particle.scale;
		}

		particle.parentMatrix = Engine::Matrix4x4::Identity();
		particle.parentLocalFileID = {};
		particle.parentIsEmitter = false;
		particle.hasParent = false;
	}

	// ワールド位置と速度を親ローカルへ変換し、必要なら回転とスケールもワールド保持へ変換する
	void AttachParticleParent(Engine::Particle& particle, const Engine::Matrix4x4& parentMatrix,
		const Engine::Quaternion& parentRotation, const Engine::Vector3& parentScale,
		bool parentIsEmitter, Engine::UUID parentLocalFileID, bool preserveWorldRotationScale) {

		const Engine::Matrix4x4 inverseParent = Engine::Matrix4x4::Inverse(parentMatrix);
		particle.pos = Engine::Vector3::Transform(particle.pos, inverseParent);
		particle.velocity = Engine::Vector3::TransferNormal(particle.velocity, inverseParent);
		particle.spawnDirection = Engine::Vector3::NormalizeOr(
			Engine::Vector3::TransferNormal(particle.spawnDirection, inverseParent),
			Engine::Vector3(0.0f, 1.0f, 0.0f));
		if (preserveWorldRotationScale) {
			particle.rotation = Engine::Quaternion::Normalize(
				Engine::Quaternion::Inverse(parentRotation) * particle.rotation);
			particle.scale = DivideScale(particle.scale, parentScale);
		}

		particle.parentMatrix = parentMatrix;
		particle.parentLocalFileID = parentLocalFileID;
		particle.parentIsEmitter = parentIsEmitter;
		particle.hasParent = true;
	}

	// 親ローカル値をそのままワールド値として親を解除する
	void DetachParticleParentWithoutKeepingWorld(Engine::Particle& particle) {

		particle.parentMatrix = Engine::Matrix4x4::Identity();
		particle.parentLocalFileID = {};
		particle.parentIsEmitter = false;
		particle.hasParent = false;
	}

	// 再生中の全発生処理を止める
	void StopPlayback(Engine::EffectEmitterPlaybackRuntime& playback) {

		playback.stopped = true;
		for (Engine::EffectEmitterStateRuntime& state : playback.states) {

			state.scheduleFinished = true;
			for (Engine::ParticleEffectInstanceRuntime& effect : state.effects) {
				effect.emissionStopped = true;
			}
		}
	}

	// 再生基準行列を作る
	Engine::Matrix4x4 BuildPlaybackAnchor(Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::EffectEmitterPlaybackRuntime& playback) {

		if (playback.fixedAnchor) {
			return Engine::Matrix4x4::MakeAffineMatrix(Engine::Vector3::AnyInit(1.0f),
				playback.fixedRotation, playback.fixedPosition);
		}
		if (const auto* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			return transform->worldMatrix;
		}
		return Engine::Matrix4x4::Identity();
	}
}

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

	world.ForEach<EffectEmitterComponent>([&](const Entity& entity, EffectEmitterComponent& emitter) {

		const bool canPlay = context.mode == WorldMode::Play || emitter.playInEditMode;
		if (!emitter.runtimeStarted) {

			emitter.runtimeStarted = true;
			if (emitter.enabled && emitter.playOnStart && canPlay) {
				emitter.Emit();
			}
		}
		ProcessCommands(emitter);

		const float deltaTime = context.mode == WorldMode::Play ? context.deltaTime : context.unscaledDeltaTime;
		const bool updateSimulation = canPlay && 0.0f < deltaTime;
		for (auto playbackIt = emitter.runtimePlaybacks.begin(); playbackIt != emitter.runtimePlaybacks.end();) {

			EffectEmitterPlaybackRuntime& playback = *playbackIt;
			const Matrix4x4 playbackAnchor = BuildPlaybackAnchor(world, entity, playback);
			for (EffectEmitterStateRuntime& state : playback.states) {

				if (updateSimulation && emitter.enabled && !playback.stopped && !state.scheduleFinished) {
					UpdateStateSchedule(emitter, state, deltaTime);
				}
				const Matrix4x4 local = Matrix4x4::MakeAffineMatrix(
					state.state.localScale, state.state.localRotation, state.state.localPosition);
				const Matrix4x4 emitterWorld = local * playbackAnchor;
				for (auto effectIt = state.effects.begin(); effectIt != state.effects.end();) {

					const bool emissionEnabled = emitter.enabled && !playback.stopped && !effectIt->emissionStopped;
					if (UpdateEffectInstance(world, *effectIt, emitterWorld,
						state.state.parentSettings, state.useAssetParentSettings, context,
						deltaTime, updateSimulation, emissionEnabled,
						emitter.drawEmitterShape, checkReload)) {
						effectIt = state.effects.erase(effectIt);
					} else {
						++effectIt;
					}
				}
				if (state.state.mode == EffectEmitterMode::Continuous && state.state.interval <= 0.0f &&
					0 < state.emittedCount && state.effects.empty()) {
					state.scheduleFinished = true;
				}
			}

			const bool finished = std::all_of(playback.states.begin(), playback.states.end(),
				[](const EffectEmitterStateRuntime& state) {
					return state.scheduleFinished && state.effects.empty();
				});
			if (finished) {
				playbackIt = emitter.runtimePlaybacks.erase(playbackIt);
			} else {
				++playbackIt;
			}
		}
		});
}

void Engine::ParticleSystem::ProcessCommands(EffectEmitterComponent& emitter) const {

	std::vector<EffectEmitterCommand> commands = std::move(emitter.runtimeCommands);
	emitter.runtimeCommands.clear();
	for (const EffectEmitterCommand& command : commands) {

		switch (command.type) {
		case EffectEmitterCommandType::Emit: {

			if (!emitter.enabled) { break; }
			const auto found = std::find_if(emitter.groups.begin(), emitter.groups.end(),
				[&](const EffectEmitterGroup& group) { return group.name == command.groupName; });
			if (found == emitter.groups.end()) { break; }

			EffectEmitterPlaybackRuntime playback{};
			playback.id = command.playbackID;
			playback.groupName = found->name;
			playback.fixedAnchor = command.fixedAnchor;
			playback.fixedPosition = command.position;
			playback.fixedRotation = command.rotation;
			for (const EffectEmitterState& state : found->states) {

				if (!state.enabled) { continue; }
				EffectEmitterStateRuntime runtime{};
				runtime.state = state;
				playback.states.emplace_back(std::move(runtime));
			}
			if (!playback.states.empty()) {
				emitter.runtimePlaybacks.emplace_back(std::move(playback));
			}
			break;
		}
		case EffectEmitterCommandType::StopHandle:
			for (EffectEmitterPlaybackRuntime& playback : emitter.runtimePlaybacks) {
				if (playback.id == command.playbackID) { StopPlayback(playback); }
			}
			break;
		case EffectEmitterCommandType::StopGroup:
			for (EffectEmitterPlaybackRuntime& playback : emitter.runtimePlaybacks) {
				if (playback.groupName == command.groupName) { StopPlayback(playback); }
			}
			break;
		case EffectEmitterCommandType::StopAll:
			for (EffectEmitterPlaybackRuntime& playback : emitter.runtimePlaybacks) {
				StopPlayback(playback);
			}
			break;
		case EffectEmitterCommandType::ClearHandle:
			std::erase_if(emitter.runtimePlaybacks, [&](const EffectEmitterPlaybackRuntime& playback) {
				return playback.id == command.playbackID;
				});
			break;
		case EffectEmitterCommandType::ClearGroup:
			std::erase_if(emitter.runtimePlaybacks, [&](const EffectEmitterPlaybackRuntime& playback) {
				return playback.groupName == command.groupName;
				});
			break;
		case EffectEmitterCommandType::ClearAll:
			emitter.runtimePlaybacks.clear();
			break;
		}
	}
}

void Engine::ParticleSystem::UpdateStateSchedule(EffectEmitterComponent& emitter,
	EffectEmitterStateRuntime& state, float deltaTime) const {

	state.time += deltaTime;
	switch (state.state.mode) {
	case EffectEmitterMode::Once:

		if (state.emittedCount == 0 && state.state.delay <= state.time) {
			AddEffectInstance(emitter, state, true);
			state.emittedCount = 1;
			state.scheduleFinished = true;
		}
		break;
	case EffectEmitterMode::Continuous: {

		bool startedThisFrame = false;
		if (state.emittedCount == 0 && state.state.delay <= state.time) {
			AddEffectInstance(emitter, state, 0.0f < state.state.interval);
			state.emittedCount = 1;
			state.emitTimer = 0.0f;
			startedThisFrame = true;
		}
		if (!state.state.emitUntilStopped && !startedThisFrame &&
			state.state.delay + state.state.duration <= state.time) {
			state.scheduleFinished = true;
			if (state.state.interval <= 0.0f) {
				for (ParticleEffectInstanceRuntime& effect : state.effects) {
					effect.emissionStopped = true;
				}
			}
		} else if (!startedThisFrame && 0.0f < state.state.interval) {

			state.emitTimer += deltaTime;
			if (state.state.interval <= state.emitTimer) {

				AddEffectInstance(emitter, state, true);
				++state.emittedCount;
				state.emitTimer = 0.0f;
			}
		}
		break;
	}
	case EffectEmitterMode::Count:

		while (state.emittedCount < state.state.count &&
			state.state.delay + state.state.interval * static_cast<float>(state.emittedCount) <= state.time) {

			AddEffectInstance(emitter, state, true);
			++state.emittedCount;
		}
		state.scheduleFinished = state.state.count <= state.emittedCount;
		break;
	}
}

void Engine::ParticleSystem::AddEffectInstance(EffectEmitterComponent& emitter,
	EffectEmitterStateRuntime& state, bool oneShot) const {

	ParticleEffectInstanceRuntime instance{};
	instance.id = emitter.runtimeNextEffectInstanceID++;
	if (instance.id == 0) { instance.id = emitter.runtimeNextEffectInstanceID++; }
	instance.effect = state.state.effect;
	instance.oneShot = oneShot;
	state.effects.emplace_back(std::move(instance));
}

void Engine::ParticleSystem::SynchronizeRuntimeGroups(
	ParticleEffectInstanceRuntime& instance, const EffectRuntime& effect) const {

	bool matched = instance.runtimeGroups.size() == effect.asset.groups.size();
	for (size_t i = 0; matched && i < effect.asset.groups.size(); ++i) {
		matched = instance.runtimeGroups[i].groupID == effect.asset.groups[i].id;
	}
	if (matched && instance.runtimeEffectRevision == effect.revision) { return; }
	if (matched) {

		for (size_t i = 0; i < effect.asset.groups.size(); ++i) {
			instance.runtimeGroups[i].renderSettings =
				MakeParticleRenderSettings(effect.asset.space, effect.asset.groups[i]);
		}
		instance.runtimeEffectRevision = effect.revision;
		return;
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
	instance.runtimeSpace = asset.space;
	const bool newRuntime = instance.runtimeGroups.empty();
	SynchronizeRuntimeGroups(instance, *effect);
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

const Engine::ParticlePhaseParentSettings& Engine::ParticleSystem::ResolveParticleParentSettings(
	const PhaseRuntime& phase, const ParticlePhaseParentSettings& parentSettings,
	bool useAssetParentSettings) const {

	return useAssetParentSettings ? phase.parentSettings : parentSettings;
}

void Engine::ParticleSystem::UpdateParticleParent(Particle& particle,
	const ParticlePhaseParentSettings& settings, const ParentRuntime& parent,
	bool preserveWorldRotationScale) const {

	if (!parent.resolved) {

		if (particle.hasParent) {
			// 親が外れたときは設定に従ってワールドへ退避する
			if (settings.keepWorldOnDetach) {
				BakeParticleParentToWorld(particle);
			} else {
				DetachParticleParentWithoutKeepingWorld(particle);
			}
		}
		return;
	}

	const bool sameParent = particle.hasParent &&
		particle.parentIsEmitter == settings.useEmitter &&
		(settings.useEmitter || particle.parentLocalFileID == settings.entityLocalFileID);
	if (sameParent) {

		// ローカル姿勢は維持し、最新の親行列だけを反映する
		particle.parentMatrix = parent.matrix;
		return;
	}

	// 親の付け替えは一度ワールドへ戻してから新しい親へ変換する
	if (particle.hasParent) {
		BakeParticleParentToWorld(particle);
	}
	AttachParticleParent(particle, parent.matrix, parent.rotation, parent.scale, settings.useEmitter,
		settings.useEmitter ? UUID{} : settings.entityLocalFileID, preserveWorldRotationScale);
}

void Engine::ParticleSystem::UpdateParticleParents(std::vector<Particle>& particles,
	const GroupRuntime& group, const ParticlePhaseParentSettings& parentSettings,
	bool useAssetParentSettings, const std::vector<ParentRuntime>& parents) const {

	for (Particle& particle : particles) {

		if (particle.phaseIndex < group.phases.size()) {
			const PhaseRuntime& phase = group.phases[particle.phaseIndex];
			UpdateParticleParent(particle, ResolveParticleParentSettings(
				phase, parentSettings, useAssetParentSettings), parents[particle.phaseIndex]);
		} else if (particle.hasParent) {
			BakeParticleParentToWorld(particle);
		}
		const ParentRuntime* parent = particle.phaseIndex < parents.size() ?
			&parents[particle.phaseIndex] : nullptr;
		RefreshParticleWorldTransform(particle, parent);
	}
}

void Engine::ParticleSystem::ResolveParticleParents(ECSWorld& world, const Matrix4x4& emitterWorld,
	const GroupRuntime& group, const ParticlePhaseParentSettings& parentSettings,
	bool useAssetParentSettings,
	std::vector<ParentRuntime>& outParents) const {

	constexpr float kMinScale = 1.0e-6f;
	for (size_t i = 0; i < group.phases.size(); ++i) {

		const ParticlePhaseParentSettings& settings = ResolveParticleParentSettings(
			group.phases[i], parentSettings, useAssetParentSettings);
		if (!settings.HasParent()) {
			continue;
		}
		Matrix4x4 parentWorld = emitterWorld;
		if (!settings.useEmitter) {

			const Entity parentEntity = SceneObjectUtility::FindByLocalFileID(world, settings.entityLocalFileID);
			if (!world.IsAlive(parentEntity)) { continue; }
			const TransformComponent* transform = world.TryGetComponent<TransformComponent>(parentEntity);
			if (!transform) { continue; }
			parentWorld = transform->worldMatrix;
		}

		ParentRuntime& parent = outParents[i];
		parent.matrix = BuildParentFollowMatrix(parentWorld,
			settings.ignoreParentScale, settings.ignoreParentRotation);
		if (!DecomposeParentMatrix(parent.matrix, parent.rotation, parent.scale) ||
			std::abs(parent.scale.x) <= kMinScale ||
			std::abs(parent.scale.y) <= kMinScale ||
			std::abs(parent.scale.z) <= kMinScale) {
			continue;
		}
		parent.resolved = true;
	}
}

void Engine::ParticleSystem::RefreshParticleWorldTransform(Particle& particle,
	const ParentRuntime* parent) const {

	if (!particle.hasParent || !parent || !parent->resolved) {

		particle.worldPos = particle.pos;
		particle.worldRotation = particle.rotation;
		particle.worldScale = particle.scale;
		return;
	}

	particle.worldPos = Vector3::Transform(particle.pos, particle.parentMatrix);
	particle.worldRotation = Quaternion::Normalize(parent->rotation * particle.rotation);
	particle.worldScale = parent->scale * particle.scale;
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

void Engine::ParticleSystem::RecordTrails(ParticleGroupRuntimeState& state,
	const ParticleTrailSettings& trail, float deltaTime) {

	// 生存粒子のIDを収集する
	aliveTrailIDs_.clear();
	aliveTrailIDs_.reserve(state.particles.size());
	for (const Particle& particle : state.particles) {
		aliveTrailIDs_.insert(particle.id);
	}

	// 軌跡点を老化させ、死亡した粒子のトレイルは切り離す
	const bool keepAfterParticleDeath = trail.keepAfterParticleDeath && 0.0f < trail.pointLifetime;
	for (auto it = state.trails.begin(); it != state.trails.end();) {

		ParticleTrailRuntime& runtime = it->second;
		const bool alive = aliveTrailIDs_.contains(it->first);
		if (!alive && !keepAfterParticleDeath) {
			it = state.trails.erase(it);
			continue;
		}
		runtime.detached = !alive;
		if (alive) {
			runtime.hasOwner = false;
			runtime.detachedThisFrame = false;
		}
		for (ParticleTrailPoint& point : runtime.points) {
			point.age += deltaTime;
		}
		if (runtime.detached) {
			runtime.head.age += deltaTime;
		}
		if (0.0f < trail.pointLifetime) {
			while (!runtime.points.empty() && trail.pointLifetime < runtime.points.front().age) {
				runtime.points.pop_front();
			}
		}
		if (runtime.detached && runtime.points.empty()) {
			it = state.trails.erase(it);
			continue;
		}
		++it;
	}

	// 一定距離を移動した粒子へ軌跡点を追加する、上限を超えたら古い点を捨てる
	const int32_t maxPoints = (std::max)(trail.maxPoints, 2);
	for (const Particle& particle : state.particles) {

		const Vector3 worldPos = particle.worldPos;
		ParticleTrailRuntime& runtime = state.trails[particle.id];
		runtime.head = ParticleTrailPoint{ worldPos, 0.0f, particle.phaseIndex };
		runtime.detached = false;
		runtime.hasOwner = false;
		runtime.detachedThisFrame = false;
		std::deque<ParticleTrailPoint>& points = runtime.points;

		if (points.empty()) {
			points.emplace_back(runtime.head);
			continue;
		}
		const Vector3 diff = worldPos - points.back().position;
		if (trail.minDistance * trail.minDistance <= Vector3::Dot(diff, diff)) {

			points.emplace_back(runtime.head);
			if (maxPoints < static_cast<int32_t>(points.size())) {
				points.pop_front();
			}
		}
	}
}

void Engine::ParticleSystem::UpdateDetachedTrailOwners(ParticleGroupRuntimeState& state,
	const GroupRuntime& group, const ParticlePhaseParentSettings& parentSettings,
	bool useAssetParentSettings, const std::vector<ParentRuntime>& parents,
	const ParticleTrailSettings& trail, float deltaTime) const {

	if (!trail.keepAfterParticleDeath) {
		return;
	}
	for (auto& [id, runtime] : state.trails) {

		if (!runtime.detached || !runtime.hasOwner) {
			continue;
		}
		Particle& owner = runtime.owner;
		if (trail.continueUpdateAfterParticleDeath) {

			if (!runtime.detachedThisFrame) {
				owner.previousAge = owner.age;
				owner.previousPhaseIndex = owner.phaseIndex;
				owner.age += deltaTime;
			}
			if (owner.phaseIndex < group.phases.size()) {

				const PhaseRuntime& phase = group.phases[owner.phaseIndex];
				UpdateParticleParent(owner, ResolveParticleParentSettings(
					phase, parentSettings, useAssetParentSettings), parents[owner.phaseIndex]);
			}
			owner.pos += owner.velocity * deltaTime;
			if (owner.phaseIndex < group.phases.size()) {

				const PhaseRuntime& phase = group.phases[owner.phaseIndex];
				if (phase.hasUpdateBatch) {
					ExecuteUpdateModules(std::span<Particle>(&owner, 1), phase, deltaTime);
				} else {
					ApplyUpdateModules(owner, phase, deltaTime);
				}
			}
		}
		const ParentRuntime* parent = owner.phaseIndex < parents.size() ? &parents[owner.phaseIndex] : nullptr;
		RefreshParticleWorldTransform(owner, parent);
		runtime.head.position = owner.worldPos;
		runtime.head.phaseIndex = owner.phaseIndex;
		runtime.detachedThisFrame = false;
	}
}

void Engine::ParticleSystem::BuildGroups(EffectRuntime& runtime) const {

	// 未登録のモジュールは読み飛ばす
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

				auto module = ParticleModuleRegistry::GetInstance().Create(entry.id);
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
