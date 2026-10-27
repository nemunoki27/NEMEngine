#include "ParticleSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUState.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#endif

// c++
#include <algorithm>

//============================================================================
//	ParticleSystem classMethods
//============================================================================
void Engine::ParticleSystem::Update(ECSWorld& world, SystemContext& context) {

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

			if (!runtime->initialized && canPlay) {
				runtime->initialized = true;
				if (component.enabled && component.playOnAwake && !runtime->playing) {
					StartEffect(*runtime, false);
				}
			}
			ProcessCommands(component, *runtime);

			if (runtime->stopActionPending) {
				ApplyStopAction(world, entity, component, *runtime, context.mode);
			}
			if (!runtime->playing && !runtime->paused) {
				return;
			}

			const float baseDeltaTime = component.useUnscaledTime ||
				context.mode != WorldMode::Play ? context.unscaledDeltaTime : context.deltaTime;
			const float deltaTime = baseDeltaTime * (std::max)(component.playbackSpeed, 0.0f);
			// 一時停止中も親の姿勢と編集値を同期する
			const bool updateSimulation = !runtime->paused && component.enabled && canPlay && 0.0f < deltaTime;
			const bool emissionEnabled = !runtime->paused && component.enabled &&
				!runtime->instance.emissionStopped;
			ResolvedWorldTransform emitterTransform{};
			// 再生開始時だけSeed設定を実行Instanceへ渡す
			if (!runtime->instance.randomInitialized) {
				runtime->instance.randomSeed = component.randomSeed;
				runtime->instance.useAutoRandomSeed = component.useAutoRandomSeed;
			}
			const Matrix4x4 emitterWorld =
				TransformWorldUtility::ResolveWorldTransform(
					world, entity, emitterTransform) ?
				emitterTransform.matrix : Matrix4x4::Identity();

			ParticlePhaseParentSettings parentSettings{};
			bool useAssetParentSettings =
				component.simulationSpace == ParticleSystemSimulationSpace::PhaseSettings;
			switch (component.simulationSpace) {
			case ParticleSystemSimulationSpace::Local:
				parentSettings.useEmitter = true;
				break;
			case ParticleSystemSimulationSpace::PhaseSettings:
			case ParticleSystemSimulationSpace::World:
				break;
			}

			// 無効なEntityとComponentの補助線は表示しない
			const bool drawEmitterShape = active && component.enabled && component.drawEmitterShape;
			if (instanceUpdater_.UpdateInstance(world, runtime->instance,
				GetParticleSystemSettings(world, entity), emitterWorld, parentSettings, useAssetParentSettings,
				deltaTime, updateSimulation, emissionEnabled, drawEmitterShape, component.simulationTarget)) {

				runtime->playing = false;
				runtime->paused = false;
				runtime->stopped = true;
				runtime->stopActionPending = true;
				ApplyStopAction(world, entity, component, *runtime, context.mode);
			}
		});
}

void Engine::ParticleSystem::ProcessCommands(
	[[maybe_unused]] const ParticleSystemComponent& component,
	ParticleSystemRuntimeData& runtime) const {

	for (const ParticleSystemCommand& command : runtime.commands) {

		const bool gpuRejected = runtime.instance.emitter.gpu && runtime.instance.emitter.gpu->rejected;
		switch (command.type) {
		case ParticleSystemCommandType::Play:
			if (runtime.paused && !runtime.stopped && !gpuRejected) {
				runtime.paused = false;
				runtime.playing = true;
			} else if (runtime.stopped || gpuRejected) {
				// GPUの実行失敗後も再生操作で設定を再検証する
				StartEffect(runtime, command.oneShot);
			}
			break;
		case ParticleSystemCommandType::Pause:
			if (runtime.playing && !runtime.stopped) {
				runtime.playing = false;
				runtime.paused = true;
			}
			break;
		case ParticleSystemCommandType::Stop:
			runtime.instance.emissionStopped = true;
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
			StartEffect(runtime, command.oneShot);
			break;
		}
	}
	runtime.commands.clear();
}

void Engine::ParticleSystem::StartEffect(ParticleSystemRuntimeData& runtime,
	bool oneShot) const {

	runtime.instance = ParticleInstanceState{};
	runtime.instance.oneShot = oneShot;
	runtime.playing = true;
	runtime.paused = false;
	runtime.stopped = false;
	runtime.stopActionPending = false;
}

void Engine::ParticleSystem::ClearEffect(
	ParticleSystemRuntimeData& runtime) const {

	runtime.instance.emitter.particles.clear();
	runtime.instance.emitter.trails.clear();
	if (auto& gpu = runtime.instance.emitter.gpu) {
		++gpu->epoch;
		gpu->pending.clear();
		gpu->statisticsValid = false;
		gpu->emissionEndSequence = UINT64_MAX;
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
