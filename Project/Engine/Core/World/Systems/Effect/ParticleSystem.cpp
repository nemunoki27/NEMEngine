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

			if (instanceUpdater_.UpdateEffectInstance(world, runtime->effect, emitterWorld,
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
