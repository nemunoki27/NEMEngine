#include "ClothComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/World/PendingStorageCleanup.h>

// c++
#include <algorithm>
#include <cmath>

using namespace Engine;

namespace {

	ClothRecord& EnsureCloth(ECSWorld& world, Entity entity, ClothComponent& component) {

		auto& storage = world.GetStorage().Get<ClothStorage>();
		const auto owner = world.GetUUID(entity);
		auto* record = storage.TryGet(component.handle);
		if (record && record->owner == owner) return *record;
		component.handle = storage.Emplace(ClothRecord{record ? record->settings : ClothSettings{}, {}, owner});
		return *storage.TryGet(component.handle);
	}

	void ClearSimulation(ClothRuntime& runtime) {

		const auto revision = runtime.revision;
		runtime = {};
		runtime.revision = revision + 1;
	}

	void ApplySettings(ClothRecord& record, ClothSettings next) {

		const auto& previous = record.settings;
		next.fasteners = std::clamp(next.fasteners, 2, kMaxPrimitiveDivide + 1);
		const ClothSettings defaults;
		auto bounded = [](float value, float fallback, float lo, float hi) {
			return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
		};
		next.foldDepth = bounded(next.foldDepth, defaults.foldDepth, 0.0f, 1000.0f);
		next.stretchCompliance = bounded(next.stretchCompliance, defaults.stretchCompliance, 0.0f, 1.0f);
		next.shearCompliance = bounded(next.shearCompliance, defaults.shearCompliance, 0.0f, 1.0f);
		next.bendCompliance = bounded(next.bendCompliance, defaults.bendCompliance, 0.0f, 1.0f);
		next.damping = bounded(next.damping, defaults.damping, 0.0f, 100.0f);
		next.gravityScale = bounded(next.gravityScale, defaults.gravityScale, -100.0f, 100.0f);
		next.windStrength = bounded(next.windStrength, defaults.windStrength, 0.0f, 100.0f);
		next.gustStrength = bounded(next.gustStrength, defaults.gustStrength, 0.0f, 100.0f);
		next.gustFrequency = bounded(next.gustFrequency, defaults.gustFrequency, 0.0f, 100.0f);
		next.thickness = bounded(next.thickness, defaults.thickness, 0.0f, 10.0f);
		if (!std::isfinite(next.windDirection.x) || !std::isfinite(next.windDirection.y) ||
			!std::isfinite(next.windDirection.z)) next.windDirection = defaults.windDirection;
		if (previous.pinMode != next.pinMode || previous.edge != next.edge || previous.foldDepth != next.foldDepth ||
			previous.fasteners != next.fasteners || previous.enabled != next.enabled ||
			previous.playInEditMode != next.playInEditMode) ClearSimulation(record.runtime);
		record.settings = std::move(next);
	}
}

bool Engine::IsClothPlane(const PrimitiveRendererComponent* renderer) {

	return renderer && renderer->renderSpace == PrimitiveRenderSpace::World3D && renderer->type == PrimitiveType::Plane;
}

void Engine::to_json(nlohmann::json& out, const ClothSettings& s) {

	out = {{"pinMode", EnumAdapter<ClothPinMode>::ToString(s.pinMode)}, {"edge", EnumAdapter<ClothEdge>::ToString(s.edge)},
		{"enabled", s.enabled}, {"playInEditMode", s.playInEditMode}, {"windDirection", s.windDirection.ToJson()},
		{"fasteners", s.fasteners}, {"foldDepth", s.foldDepth}, {"stretchCompliance", s.stretchCompliance},
		{"shearCompliance", s.shearCompliance},
		{"bendCompliance", s.bendCompliance}, {"damping", s.damping}, {"gravityScale", s.gravityScale},
		{"windStrength", s.windStrength}, {"gustStrength", s.gustStrength}, {"gustFrequency", s.gustFrequency},
		{"thickness", s.thickness}, {"collisionMask", s.collisionMask}};
}

void Engine::from_json(const nlohmann::json& in, ClothSettings& s) {

	s.pinMode = EnumAdapter<ClothPinMode>::FromString(in.value("pinMode", "")).value_or(s.pinMode);
	s.edge = EnumAdapter<ClothEdge>::FromString(in.value("edge", "")).value_or(s.edge);
	if (in.contains("windDirection")) s.windDirection = Vector3::FromJson(in.at("windDirection"));
	s.enabled = in.value("enabled", s.enabled);
	s.playInEditMode = in.value("playInEditMode", s.playInEditMode);
	s.fasteners = in.value("fasteners", s.fasteners);
	s.foldDepth = in.value("foldDepth", s.foldDepth);
	s.stretchCompliance = in.value("stretchCompliance", s.stretchCompliance);
	s.shearCompliance = in.value("shearCompliance", s.shearCompliance);
	s.bendCompliance = in.value("bendCompliance", s.bendCompliance);
	s.damping = in.value("damping", s.damping);
	s.gravityScale = in.value("gravityScale", s.gravityScale);
	s.windStrength = in.value("windStrength", s.windStrength);
	s.gustStrength = in.value("gustStrength", s.gustStrength);
	s.gustFrequency = in.value("gustFrequency", s.gustFrequency);
	s.thickness = in.value("thickness", s.thickness);
	s.collisionMask = in.value("collisionMask", s.collisionMask);
}

void ClothComponent::InitializeStorage(ECSWorld& world, const Entity& owner, ClothComponent& component) {

	EnsureCloth(world, owner, component);
}

void ClothComponent::OnAdded(ECSWorld& world, const Entity& owner, ClothComponent& component) {

	InitializeStorage(world, owner, component);
}

void ClothComponent::OnRemoved(ECSWorld&, const Entity&) {}

void ClothComponent::ReleaseStorage(ECSWorld& world, const Entity& owner, ClothComponent& component) {

	auto& storage = world.GetStorage().Get<ClothStorage>();
	const auto* record = storage.TryGet(component.handle);
	if (record && record->owner == world.GetUUID(owner)) storage.Release(component.handle);
	component.handle = {};
}

void ClothComponent::CloneStorage(const ECSWorld& sourceWorld, ECSWorld& destinationWorld, const Entity& owner,
	const ClothComponent& source, ClothComponent& destination) {

	const auto* storage = sourceWorld.GetStorage().TryGet<ClothStorage>();
	const auto* record = storage ? storage->TryGet(source.handle) : nullptr;
	destination.handle = destinationWorld.GetStorage().Get<ClothStorage>().Emplace(
		ClothRecord{record ? record->settings : ClothSettings{}, {}, destinationWorld.GetUUID(owner)});
}


void ClothComponent::DeserializeECS(ECSWorld& world, const Entity& owner, const nlohmann::json& in, ClothComponent& component) {

	ApplySettings(EnsureCloth(world, owner, component), in.get<ClothSettings>());
}

void ClothComponent::SerializeECS(const ECSWorld& world, const Entity&, const ClothComponent& component, nlohmann::json& out) {

	const auto* storage = world.GetStorage().TryGet<ClothStorage>();
	const auto* record = storage ? storage->TryGet(component.handle) : nullptr;
	to_json(out, record ? record->settings : ClothSettings{});
}

const ClothRecord* Engine::TryGetCloth(const ECSWorld& world, Entity owner) {

	const auto* component = world.TryGetComponentForBinding<ClothComponent>(owner);
	const auto* storage = world.GetStorage().TryGet<ClothStorage>();
	return component && storage ? storage->TryGet(component->handle) : nullptr;
}

ClothRecord* Engine::TryGetCloth(ECSWorld& world, Entity owner) {

	auto* component = world.TryGetComponentForBinding<ClothComponent>(owner);
	if (!component) return nullptr;
	auto& record = EnsureCloth(world, owner, *component);
	TrackPendingStorage<ClothComponent, ClothStorage>(world, owner, &ClothComponent::handle);
	return &record;
}


void Engine::SetClothSettings(ECSWorld& world, Entity owner, const ClothSettings& settings) {

	if (auto* record = TryGetCloth(world, owner)) {
		ApplySettings(*record, settings);
		world.MarkComponentModified<ClothComponent>(owner);
	}
}

void Engine::ResetCloth(ECSWorld& world, Entity owner) {

	if (auto* record = TryGetCloth(world, owner)) {
		ClearSimulation(record->runtime);
		world.MarkRenderDataModified(owner);
	}
}
