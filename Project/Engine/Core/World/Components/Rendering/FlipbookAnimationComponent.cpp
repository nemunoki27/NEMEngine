#include "FlipbookAnimationComponent.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Utility/Flipbook/FlipbookTileLayout.h>

// c++
#include <algorithm>
#include <vector>

//============================================================================
//	FlipbookAnimationComponent structMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, FlipbookAnimationComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.loop = in.value("loop", component.loop);
	component.loopInterval = in.value("loopInterval", component.loopInterval);
	component.playInEditMode = in.value("playInEditMode", component.playInEditMode);
	component.endAnimUnDisplay = in.value("endAnimUnDisplay", component.endAnimUnDisplay);
	component.tilesY = in.value("tilesY", component.tilesY);
	component.duration = in.value("duration", component.duration);
	component.easingType = EnumAdapter<EasingType>::FromString(
		in.value("easingType", "EaseInSine")).value_or(EasingType::EaseInSine);
}

void Engine::to_json(nlohmann::json& out, const FlipbookAnimationComponent& component) {

	out["enabled"] = component.enabled;
	out["loop"] = component.loop;
	out["loopInterval"] = component.loopInterval;
	out["playInEditMode"] = component.playInEditMode;
	out["endAnimUnDisplay"] = component.endAnimUnDisplay;
	out["tilesY"] = component.tilesY;
	out["duration"] = component.duration;
	out["easingType"] = EnumAdapter<EasingType>::ToString(component.easingType);
}

void Engine::FlipbookAnimationComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] FlipbookAnimationComponent& component) {

	DynamicBuffer<FlipbookTileColumn> columns =
		world.AddBuffer<FlipbookTileColumn>(entity);
	if (columns.IsEmpty()) {
		columns.Add(FlipbookTileColumn{});
	}
	if (!world.HasComponent<FlipbookAnimationRuntimeComponent>(entity)) {
		world.AddComponent<FlipbookAnimationRuntimeComponent>(entity);
	}
}

void Engine::FlipbookAnimationComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	if (world.HasBuffer<FlipbookTileColumn>(entity)) {
		world.RemoveBuffer<FlipbookTileColumn>(entity);
	}
	if (world.HasComponent<FlipbookAnimationRuntimeComponent>(entity)) {
		world.RemoveComponent<FlipbookAnimationRuntimeComponent>(entity);
	}
}

void Engine::FlipbookAnimationComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] FlipbookAnimationComponent& component) {
}

void Engine::FlipbookAnimationComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] FlipbookAnimationComponent& component) {
}

void Engine::FlipbookAnimationComponent::DeserializeECS(
	ECSWorld& world, const Entity& entity, const nlohmann::json& in,
	FlipbookAnimationComponent& component) {

	from_json(in, component);
	std::vector<int32_t> columns{};
	ReadFlipbookTileLayout(in, columns, component.tilesY);
	SetFlipbookTileColumns(world, entity, columns);
}

void Engine::FlipbookAnimationComponent::SerializeECS(
	const ECSWorld& world, const Entity& entity,
	const FlipbookAnimationComponent& component, nlohmann::json& out) {

	const std::span<const FlipbookTileColumn> stored =
		GetFlipbookTileColumns(world, entity);
	std::vector<int32_t> columns{};
	columns.reserve(stored.size());
	for (const FlipbookTileColumn& column : stored) {
		columns.emplace_back(column.value);
	}
	SerializeFlipbookAnimation(component, columns, out);
}

std::span<Engine::FlipbookTileColumn> Engine::GetFlipbookTileColumns(
	ECSWorld& world, const Entity& entity) {

	return world.TryGetBuffer<FlipbookTileColumn>(entity).GetSpan();
}

std::span<const Engine::FlipbookTileColumn> Engine::GetFlipbookTileColumns(
	const ECSWorld& world, const Entity& entity) {

	return world.GetBufferSpan<FlipbookTileColumn>(entity);
}

std::span<const int32_t> Engine::GetFlipbookTileValues(
	const ECSWorld& world, const Entity& entity) {

	static_assert(sizeof(FlipbookTileColumn) == sizeof(int32_t));
	const std::span<const FlipbookTileColumn> columns =
		GetFlipbookTileColumns(world, entity);
	return {
		reinterpret_cast<const int32_t*>(columns.data()),
		columns.size()
	};
}

void Engine::SetFlipbookTileColumns(
	ECSWorld& world, const Entity& entity,
	std::span<const int32_t> columns) {

	DynamicBuffer<FlipbookTileColumn> buffer =
		world.TryGetBuffer<FlipbookTileColumn>(entity);
	if (!buffer.IsValid()) {
		buffer = world.AddBuffer<FlipbookTileColumn>(entity);
	}
	buffer.Clear();
	buffer.Reserve(static_cast<uint32_t>((std::max)(size_t{ 1 }, columns.size())));
	if (columns.empty()) {
		buffer.Add(FlipbookTileColumn{});
	} else {
		for (int32_t column : columns) {
			buffer.Add(FlipbookTileColumn{ (std::max)(column, 1) });
		}
	}
	world.MarkComponentModified<FlipbookTileColumn>(entity);
}

void Engine::SerializeFlipbookAnimation(
	const FlipbookAnimationComponent& component,
	std::span<const int32_t> columns, nlohmann::json& out) {

	to_json(out, component);
	std::vector<int32_t> normalized(columns.begin(), columns.end());
	WriteFlipbookTileLayout(out, normalized, component.tilesY);
}
