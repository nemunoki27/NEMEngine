#include "ParticleSystemComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUState.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <limits>
#include <string>

//============================================================================
//	ParticleSystemRuntimeComponent structMethods
//============================================================================
void Engine::ParticleSystemRuntimeComponent::OnAdded(
	ECSWorld& world, [[maybe_unused]] const Entity& entity,
	ParticleSystemRuntimeComponent& component) {

	if (!component.handle.IsValid()) {
		component.handle =
			world.GetStorage().Get<ParticleSystemRuntimeStorage>().Emplace();
	}
}

void Engine::ParticleSystemRuntimeComponent::InitializeStorage(
	ECSWorld& world, const Entity& entity,
	ParticleSystemRuntimeComponent& component) {

	OnAdded(world, entity, component);
}

void Engine::ParticleSystemRuntimeComponent::ReleaseStorage(
	ECSWorld& world, [[maybe_unused]] const Entity& entity,
	ParticleSystemRuntimeComponent& component) {

	if (!component.handle.IsValid()) {
		return;
	}
	world.GetStorage().Get<ParticleSystemRuntimeStorage>().Release(component.handle);
	component.handle = ParticleSystemRuntimeHandle::Null();
}

void Engine::ParticleSystemRuntimeComponent::DeserializeECS(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] const nlohmann::json& in,
	[[maybe_unused]] ParticleSystemRuntimeComponent& component) {
}

void Engine::ParticleSystemRuntimeComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	[[maybe_unused]] const ParticleSystemRuntimeComponent& component,
	nlohmann::json& out) {

	out = nlohmann::json::object();
}

//============================================================================
//	ParticleSystemComponent structMethods
//============================================================================
void Engine::ParticleSystemComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] ParticleSystemComponent& component) {

	if (!world.HasComponent<ParticleSystemRuntimeComponent>(entity)) {
		world.AddComponent<ParticleSystemRuntimeComponent>(entity);
	}
}

void Engine::ParticleSystemComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	if (world.HasComponent<ParticleSystemRuntimeComponent>(entity)) {
		world.RemoveComponent<ParticleSystemRuntimeComponent>(entity);
	}
}

Engine::ParticleSystemRuntimeData* Engine::TryGetParticleSystemRuntime(
	ECSWorld& world, const Entity& entity) {

	ParticleSystemRuntimeComponent* runtime =
		world.TryGetComponent<ParticleSystemRuntimeComponent>(entity);
	if (!runtime) {
		return nullptr;
	}
	return world.GetStorage().Get<ParticleSystemRuntimeStorage>().TryGet(
		runtime->handle);
}

const Engine::ParticleSystemRuntimeData* Engine::TryGetParticleSystemRuntime(
	const ECSWorld& world, const Entity& entity) {

	const ParticleSystemRuntimeComponent* runtime =
		world.TryGetComponent<ParticleSystemRuntimeComponent>(entity);
	const ParticleSystemRuntimeStorage* storage =
		world.GetStorage().TryGet<ParticleSystemRuntimeStorage>();
	return runtime && storage ? storage->TryGet(runtime->handle) : nullptr;
}

void Engine::RequestParticleSystemPlay(
	ECSWorld& world, const Entity& entity, bool oneShot) {

	ParticleSystemRuntimeData* runtime = TryGetParticleSystemRuntime(world, entity);
	if (!runtime) {
		return;
	}
	runtime->commands.emplace_back(ParticleSystemCommand{
		.type = ParticleSystemCommandType::Play,
		.oneShot = oneShot,
		});
}

void Engine::RequestParticleSystemPause(
	ECSWorld& world, const Entity& entity) {

	ParticleSystemRuntimeData* runtime = TryGetParticleSystemRuntime(world, entity);
	if (runtime) {
		runtime->commands.emplace_back(ParticleSystemCommand{
			.type = ParticleSystemCommandType::Pause,
			});
	}
}

void Engine::RequestParticleSystemStop(ECSWorld& world, const Entity& entity,
	ParticleSystemStopBehavior behavior) {

	ParticleSystemRuntimeData* runtime = TryGetParticleSystemRuntime(world, entity);
	if (runtime) {
		runtime->commands.emplace_back(ParticleSystemCommand{
			.type = ParticleSystemCommandType::Stop,
			.stopBehavior = behavior,
			});
	}
}

void Engine::RequestParticleSystemClear(
	ECSWorld& world, const Entity& entity) {

	ParticleSystemRuntimeData* runtime = TryGetParticleSystemRuntime(world, entity);
	if (runtime) {
		runtime->commands.emplace_back(ParticleSystemCommand{
			.type = ParticleSystemCommandType::Clear,
			});
	}
}

void Engine::RequestParticleSystemRestart(
	ECSWorld& world, const Entity& entity, bool oneShot) {

	ParticleSystemRuntimeData* runtime = TryGetParticleSystemRuntime(world, entity);
	if (runtime) {
		runtime->commands.emplace_back(ParticleSystemCommand{
			.type = ParticleSystemCommandType::Restart,
			.oneShot = oneShot,
			});
	}
}

bool Engine::IsParticleSystemPlaying(
	const ECSWorld& world, const Entity& entity) {

	const ParticleSystemRuntimeData* runtime =
		TryGetParticleSystemRuntime(world, entity);
	return runtime && (!runtime->instance.emitter.gpu || !runtime->instance.emitter.gpu->rejected) &&
		runtime->playing && !runtime->paused &&
		!runtime->stopped;
}

bool Engine::IsParticleSystemEmitting(
	const ECSWorld& world, const Entity& entity) {

	const ParticleSystemRuntimeData* runtime =
		TryGetParticleSystemRuntime(world, entity);
	return runtime && (!runtime->instance.emitter.gpu || !runtime->instance.emitter.gpu->rejected) && runtime->playing && !runtime->paused &&
		!runtime->stopped && !runtime->instance.emissionStopped;
}

bool Engine::IsParticleSystemPaused(
	const ECSWorld& world, const Entity& entity) {

	const ParticleSystemRuntimeData* runtime =
		TryGetParticleSystemRuntime(world, entity);
	return runtime && runtime->paused;
}

bool Engine::IsParticleSystemStopped(
	const ECSWorld& world, const Entity& entity) {

	const ParticleSystemRuntimeData* runtime =
		TryGetParticleSystemRuntime(world, entity);
	return !runtime || runtime->stopped || (runtime->instance.emitter.gpu && runtime->instance.emitter.gpu->rejected);
}

bool Engine::IsParticleSystemAlive(
	const ECSWorld& world, const Entity& entity) {

	const ParticleSystemRuntimeData* runtime =
		TryGetParticleSystemRuntime(world, entity);
	if (!runtime) {
		return false;
	}
	if (runtime->instance.emitter.gpu && runtime->instance.emitter.gpu->rejected) return false;
	if ((runtime->playing && !runtime->stopped) || runtime->paused) {
		return true;
	}
	if (const auto& gpu = runtime->instance.emitter.gpu) {
		return !gpu->pending.empty() || !gpu->statisticsValid ||
			gpu->statistics.particleCount != 0 || gpu->statistics.trailCount != 0;
	}
	return !runtime->instance.emitter.particles.empty() ||
		!runtime->instance.emitter.trails.empty();
}

int32_t Engine::GetParticleSystemParticleCount(
	const ECSWorld& world, const Entity& entity) {

	const ParticleSystemRuntimeData* runtime =
		TryGetParticleSystemRuntime(world, entity);
	if (!runtime) {
		return 0;
	}

	const auto& gpu = runtime->instance.emitter.gpu;
	const size_t count = gpu ? gpu->statistics.particleCount : runtime->instance.emitter.particles.size();
	return static_cast<int32_t>((std::min)(count,
		static_cast<size_t>((std::numeric_limits<int32_t>::max)())));
}

bool Engine::HasParticleSystemStatistics(const ECSWorld& world, const Entity& entity) {

	const auto* runtime = TryGetParticleSystemRuntime(world, entity);
	return runtime && (!runtime->instance.emitter.gpu || runtime->instance.emitter.gpu->statisticsValid);
}

int32_t Engine::GetParticleSystemTrailCount(const ECSWorld& world, const Entity& entity) {

	const auto* runtime = TryGetParticleSystemRuntime(world, entity);
	if (!runtime) return 0;
	const auto& gpu = runtime->instance.emitter.gpu;
	return static_cast<int32_t>(gpu ? gpu->statistics.trailCount : runtime->instance.emitter.trails.GetHandles().size());
}

int32_t Engine::GetParticleSystemDroppedTrailCount(const ECSWorld& world, const Entity& entity) {

	const auto* runtime = TryGetParticleSystemRuntime(world, entity);
	if (!runtime) return 0;
	const auto& gpu = runtime->instance.emitter.gpu;
	const uint64_t count = gpu ? gpu->statistics.overflowCount : runtime->instance.emitter.trails.GetOverflowCount();
	return static_cast<int32_t>((std::min)(count, uint64_t(INT32_MAX)));
}

void Engine::from_json(
	const nlohmann::json& in, ParticleSystemComponent& component) {

	component.simulationTarget = EnumAdapter<ParticleSimulationTarget>::FromString(
		in.value("simulationTarget", "CPU")).value_or(ParticleSimulationTarget::CPU);
	component.playbackSpeed = (std::max)(in.value("playbackSpeed", 1.0f), 0.0f);
	component.layer = in.value("layer", 0);
	component.order = in.value("order", 0);
	component.renderingLayerMask = in.value("renderingLayerMask", component.renderingLayerMask) & kRenderingLayerMaskBits;
	component.stopAction = EnumAdapter<ParticleSystemStopAction>::FromString(
		in.value("stopAction", "None")).value_or(ParticleSystemStopAction::None);
	component.simulationSpace = EnumAdapter<ParticleSystemSimulationSpace>::FromString(
		in.value("simulationSpace", "PhaseSettings")).value_or(
			ParticleSystemSimulationSpace::PhaseSettings);
	component.enabled = in.value("enabled", true);
	component.playOnAwake = in.value("playOnAwake", true);
	component.playInEditMode = in.value("playInEditMode", true);
	component.useUnscaledTime = in.value("useUnscaledTime", false);
	component.useAutoRandomSeed = in.value("useAutoRandomSeed", true);
	component.randomSeed = in.value("randomSeed", 0u);
	component.drawEmitterShape = in.value("drawEmitterShape", false);
	component.visible = in.value("visible", true);
}

void Engine::to_json(
	nlohmann::json& out, const ParticleSystemComponent& component) {

	out["simulationTarget"] = EnumAdapter<ParticleSimulationTarget>::ToString(component.simulationTarget);
	out["playbackSpeed"] = component.playbackSpeed;
	out["layer"] = component.layer;
	out["order"] = component.order;
	out["renderingLayerMask"] = component.renderingLayerMask & kRenderingLayerMaskBits;
	out["stopAction"] = EnumAdapter<ParticleSystemStopAction>::ToString(component.stopAction);
	out["simulationSpace"] =
		EnumAdapter<ParticleSystemSimulationSpace>::ToString(component.simulationSpace);
	out["enabled"] = component.enabled;
	out["playOnAwake"] = component.playOnAwake;
	out["playInEditMode"] = component.playInEditMode;
	out["useUnscaledTime"] = component.useUnscaledTime;
	out["useAutoRandomSeed"] = component.useAutoRandomSeed;
	out["randomSeed"] = component.randomSeed;
	out["drawEmitterShape"] = component.drawEmitterShape;
	out["visible"] = component.visible;
}
