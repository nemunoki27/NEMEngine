#include "FillFaceMeshRendererComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <vector>

//============================================================================
//	FillFaceMeshRendererComponent structMethods
//============================================================================
namespace {

	void ReadFillMeshSettings(const nlohmann::json& in,
		Engine::FillMeshRendererComponent& component) {

		component.buildMesh = in.value("buildMesh", component.buildMesh);
		component.material = Engine::ParseAssetID(in, "material");
		Engine::ReadMaterialParameterOverrides(
			in.value("parameterOverrides", nlohmann::json::object()),
			component.parameterOverrides);
		component.color = Engine::Color4::FromJson(
			in.value("color", nlohmann::json()));
		Engine::ReadRenderCommonFields(in, component.layer, component.order,
			component.visible, component.blendMode, component.queue);
	}

	void WriteFillMeshSettings(nlohmann::json& out,
		const Engine::FillMeshRendererComponent& component) {

		out["buildMesh"] = component.buildMesh;
		out["material"] = Engine::ToAssetReferenceJson(component.material);
		out["parameterOverrides"] =
			Engine::WriteMaterialParameterOverrides(component.parameterOverrides);
		out["color"] = component.color.ToJson();
		Engine::WriteRenderCommonFields(out, component.layer, component.order,
			component.visible, component.blendMode, component.queue);
	}

	std::vector<Engine::Vector3> ReadFillMeshPositions(
		const nlohmann::json& in) {

		std::vector<Engine::Vector3> positions{};
		const auto it = in.find("facePositions");
		if (it == in.end() || !it->is_array()) {
			return positions;
		}
		positions.reserve(it->size());
		for (const nlohmann::json& point : *it) {
			positions.emplace_back(Engine::Vector3::FromJson(point));
		}
		return positions;
	}
}

void Engine::FillMeshRendererComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] FillMeshRendererComponent& component) {

	if (!world.HasBuffer<FillMeshPosition>(entity)) {
		world.AddBuffer<FillMeshPosition>(entity);
	}
	if (!world.HasBuffer<FillMeshTriangleIndex>(entity)) {
		world.AddBuffer<FillMeshTriangleIndex>(entity);
	}
	if (!world.HasComponent<FillMeshRuntimeStateComponent>(entity)) {
		world.AddComponent<FillMeshRuntimeStateComponent>(entity);
	}
}

void Engine::FillMeshRendererComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	if (world.HasBuffer<FillMeshPosition>(entity)) {
		world.RemoveBuffer<FillMeshPosition>(entity);
	}
	if (world.HasBuffer<FillMeshTriangleIndex>(entity)) {
		world.RemoveBuffer<FillMeshTriangleIndex>(entity);
	}
	if (world.HasComponent<FillMeshRuntimeStateComponent>(entity)) {
		world.RemoveComponent<FillMeshRuntimeStateComponent>(entity);
	}
}

void Engine::FillMeshRendererComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] FillMeshRendererComponent& component) {
}

void Engine::FillMeshRendererComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] FillMeshRendererComponent& component) {
}

void Engine::FillMeshRendererComponent::DeserializeECS(
	ECSWorld& world, const Entity& entity, const nlohmann::json& in,
	FillMeshRendererComponent& component) {

	ReadFillMeshSettings(in, component);
	const std::vector<Vector3> positions = ReadFillMeshPositions(in);
	SetFillMeshPositions(world, entity, positions);
}

void Engine::FillMeshRendererComponent::SerializeECS(
	const ECSWorld& world, const Entity& entity,
	const FillMeshRendererComponent& component, nlohmann::json& out) {

	SerializeFillMeshRenderer(
		component, GetFillMeshPositions(world, entity), out);
}

void Engine::from_json(const nlohmann::json& in, FillMeshRendererComponent& component) {

	ReadFillMeshSettings(in, component);
}

void Engine::to_json(nlohmann::json& out, const FillMeshRendererComponent& component) {

	WriteFillMeshSettings(out, component);
	out["facePositions"] = nlohmann::json::array();
}

std::span<Engine::FillMeshPosition> Engine::GetFillMeshPositions(
	ECSWorld& world, const Entity& entity) {

	return world.TryGetBuffer<FillMeshPosition>(entity).GetSpan();
}

std::span<const Engine::FillMeshPosition> Engine::GetFillMeshPositions(
	const ECSWorld& world, const Entity& entity) {

	return world.GetBufferSpan<FillMeshPosition>(entity);
}

std::span<Engine::FillMeshTriangleIndex> Engine::GetFillMeshTriangleIndices(
	ECSWorld& world, const Entity& entity) {

	return world.TryGetBuffer<FillMeshTriangleIndex>(entity).GetSpan();
}

std::span<const Engine::FillMeshTriangleIndex> Engine::GetFillMeshTriangleIndices(
	const ECSWorld& world, const Entity& entity) {

	return world.GetBufferSpan<FillMeshTriangleIndex>(entity);
}

void Engine::SetFillMeshPositions(ECSWorld& world, const Entity& entity,
	std::span<const Vector3> positions) {

	DynamicBuffer<FillMeshPosition> buffer =
		world.TryGetBuffer<FillMeshPosition>(entity);
	if (!buffer.IsValid()) {
		buffer = world.AddBuffer<FillMeshPosition>(entity);
	}
	buffer.Clear();
	buffer.Reserve(static_cast<uint32_t>(positions.size()));
	for (const Vector3& position : positions) {
		buffer.Add(FillMeshPosition{ .value = position });
	}
	world.GetComponent<FillMeshRendererComponent>(entity).buildMesh = true;
	world.MarkComponentModified<FillMeshPosition>(entity);
}

void Engine::SetFillMeshTriangleIndices(ECSWorld& world, const Entity& entity,
	std::span<const uint32_t> indices) {

	DynamicBuffer<FillMeshTriangleIndex> buffer =
		world.TryGetBuffer<FillMeshTriangleIndex>(entity);
	if (!buffer.IsValid()) {
		buffer = world.AddBuffer<FillMeshTriangleIndex>(entity);
	}
	buffer.Clear();
	buffer.Reserve(static_cast<uint32_t>(indices.size()));
	for (uint32_t index : indices) {
		buffer.Add(FillMeshTriangleIndex{ .value = index });
	}
	world.MarkComponentModified<FillMeshTriangleIndex>(entity);
}

void Engine::SerializeFillMeshRenderer(
	const FillMeshRendererComponent& component,
	std::span<const FillMeshPosition> positions, nlohmann::json& out) {

	WriteFillMeshSettings(out, component);
	out["facePositions"] = nlohmann::json::array();
	for (const FillMeshPosition& position : positions) {
		out["facePositions"].push_back(position.value.ToJson());
	}
}
