#include "ParticleSystemComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
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

void Engine::ParticleSystemComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] ParticleSystemComponent& component) {
}

void Engine::ParticleSystemComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] ParticleSystemComponent& component) {
}

void Engine::ParticleSystemComponent::DeserializeECS(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const nlohmann::json& in, ParticleSystemComponent& component) {

	from_json(in, component);
}

void Engine::ParticleSystemComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	const ParticleSystemComponent& component, nlohmann::json& out) {

	to_json(out, component);
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
	return runtime && runtime->playing && !runtime->paused &&
		!runtime->stopped;
}

bool Engine::IsParticleSystemEmitting(
	const ECSWorld& world, const Entity& entity) {

	const ParticleSystemRuntimeData* runtime =
		TryGetParticleSystemRuntime(world, entity);
	return runtime && runtime->playing && !runtime->paused &&
		!runtime->stopped && !runtime->effect.emissionStopped;
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
	return !runtime || runtime->stopped;
}

bool Engine::IsParticleSystemAlive(
	const ECSWorld& world, const Entity& entity) {

	const ParticleSystemRuntimeData* runtime =
		TryGetParticleSystemRuntime(world, entity);
	if (!runtime) {
		return false;
	}
	if ((runtime->playing && !runtime->stopped) || runtime->paused) {
		return true;
	}
	for (const ParticleGroupRuntimeState& group : runtime->effect.runtimeGroups) {
		if (!group.particles.empty() || !group.trails.empty()) {
			return true;
		}
	}
	return false;
}

int32_t Engine::GetParticleSystemParticleCount(
	const ECSWorld& world, const Entity& entity) {

	const ParticleSystemRuntimeData* runtime =
		TryGetParticleSystemRuntime(world, entity);
	if (!runtime) {
		return 0;
	}

	size_t count = 0;
	for (const ParticleGroupRuntimeState& group :
		runtime->effect.runtimeGroups) {

		count += group.particles.size();
	}
	return static_cast<int32_t>((std::min)(count,
		static_cast<size_t>((std::numeric_limits<int32_t>::max)())));
}

void Engine::from_json(
	const nlohmann::json& in, ParticleSystemComponent& component) {

	component.effect = ParseAssetID(in, "effect");
	component.playbackSpeed = (std::max)(in.value("playbackSpeed", 1.0f), 0.0f);
	component.layer = in.value("layer", 0);
	component.order = in.value("order", 0);
	component.stopAction = EnumAdapter<ParticleSystemStopAction>::FromString(
		in.value("stopAction", "None")).value_or(ParticleSystemStopAction::None);
	component.simulationSpace = EnumAdapter<ParticleSystemSimulationSpace>::FromString(
		in.value("simulationSpace", "EffectAsset")).value_or(
			ParticleSystemSimulationSpace::EffectAsset);
	const std::string customTarget = in.value("customSimulationTarget", "");
	component.customSimulationTarget = customTarget.empty() ?
		UUID{} : FromString16Hex(customTarget);
	component.enabled = in.value("enabled", true);
	component.playOnAwake = in.value("playOnAwake", true);
	component.playInEditMode = in.value("playInEditMode", true);
	component.useUnscaledTime = in.value("useUnscaledTime", false);
	component.drawEmitterShape = in.value("drawEmitterShape", false);
	component.visible = in.value("visible", true);
}

void Engine::to_json(
	nlohmann::json& out, const ParticleSystemComponent& component) {

	out["effect"] = ToAssetReferenceJson(component.effect);
	out["playbackSpeed"] = component.playbackSpeed;
	out["layer"] = component.layer;
	out["order"] = component.order;
	out["stopAction"] = EnumAdapter<ParticleSystemStopAction>::ToString(component.stopAction);
	out["simulationSpace"] =
		EnumAdapter<ParticleSystemSimulationSpace>::ToString(component.simulationSpace);
	out["customSimulationTarget"] = component.customSimulationTarget ?
		ToString(component.customSimulationTarget) : "";
	out["enabled"] = component.enabled;
	out["playOnAwake"] = component.playOnAwake;
	out["playInEditMode"] = component.playInEditMode;
	out["useUnscaledTime"] = component.useUnscaledTime;
	out["drawEmitterShape"] = component.drawEmitterShape;
	out["visible"] = component.visible;
}
