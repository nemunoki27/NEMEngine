#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticlePaintReceiverComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Rendering/Particle/ParticleSimulationValidation.h>

// c++
#include <cstring>
#include <limits>

using namespace Engine;

int32_t ManagedScriptRuntime::CopyParticleSystemSettingsCallback(ManagedNativeEntity entity, char* buffer,
	int32_t capacity, int32_t* written) {

	if (!written || capacity < 0) return static_cast<int32_t>(ManagedStatus::InvalidArgument);
	*written = 0;
	auto* world = ResolveWorld(entity);
	if (!world) return static_cast<int32_t>(ManagedStatus::InvalidWorldHandle);
	const auto settings = GetParticleSystemSettings(*world, ResolveEntity(entity));
	if (!settings) return static_cast<int32_t>(ManagedStatus::InvalidEntityHandle);
	try {
		const auto text = ToJson(*settings).dump();
		if (text.size() > (std::numeric_limits<int32_t>::max)()) return static_cast<int32_t>(ManagedStatus::SerializationError);
		*written = static_cast<int32_t>(text.size());
		if (!buffer || capacity < *written) return static_cast<int32_t>(ManagedStatus::BufferTooSmall);
		std::memcpy(buffer, text.data(), text.size());
		return static_cast<int32_t>(ManagedStatus::Ok);
	} catch (const std::exception&) { return static_cast<int32_t>(ManagedStatus::SerializationError); }
}

int32_t ManagedScriptRuntime::SetParticleSystemSettingsCallback(ManagedNativeEntity entity, const char* buffer, int32_t size) {

	if (!buffer || size <= 0 || size > 16 * 1024 * 1024) return static_cast<int32_t>(ManagedStatus::InvalidArgument);
	auto* world = ResolveWorld(entity);
	if (!world) return static_cast<int32_t>(ManagedStatus::InvalidWorldHandle);
	const auto target = ResolveEntity(entity);
	const auto* component = world->TryGetComponent<ParticleSystemComponent>(target);
	if (!component) return static_cast<int32_t>(ManagedStatus::InvalidEntityHandle);
	try {
		const auto json = nlohmann::json::parse(buffer, buffer + size);
		ParticleSystemSettings settings;
		if (!FromJson(json, settings) || !ValidateParticleSimulationSettings(settings, component->simulationTarget).empty())
			return static_cast<int32_t>(ManagedStatus::InvalidArgument);
		SetParticleSystemSettings(*world, target, settings);
		return static_cast<int32_t>(ManagedStatus::Ok);
	} catch (const std::exception&) { return static_cast<int32_t>(ManagedStatus::SerializationError); }
}

int32_t ManagedScriptRuntime::ClearParticlePaintCallback(ManagedNativeEntity entity) {

	auto* world = ResolveWorld(entity);
	const auto target = ResolveEntity(entity);
	if (!world || !world->HasComponent<ParticlePaintReceiverComponent>(target)) return 0;
	ClearParticlePaint(*world, target);
	return 1;
}
