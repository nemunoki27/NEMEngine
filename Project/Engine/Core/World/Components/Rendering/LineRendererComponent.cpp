#include "LineRendererComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <vector>

//============================================================================
//	LineRendererComponent classMethods
//============================================================================
namespace {

	void ReadLineRendererSettings(const nlohmann::json& in,
		Engine::LineRendererComponent& component) {

		component.material = Engine::ParseAssetID(in, "material");
		Engine::ReadMaterialInstance(
			in.value("materialInstance", nlohmann::json::object()),
			component.materialInstance);
		component.loop = in.value("loop", component.loop);
		component.is2D = in.value("is2D", component.is2D);
		component.useWorldSpace = in.value("useWorldSpace", component.useWorldSpace);
		component.parentLocalFileID.value =
			in.value("parentLocalFileID", component.parentLocalFileID.value);
		component.ignoreParentScale =
			in.value("ignoreParentScale", component.ignoreParentScale);
		component.ignoreParentRotation =
			in.value("ignoreParentRotation", component.ignoreParentRotation);
		Engine::ReadRenderCommonFields(in, component.layer, component.order,
			component.visible, component.blendMode, component.queue);
		component.renderingLayerMask = in.value(
			"renderingLayerMask", component.renderingLayerMask) &
			Engine::kRenderingLayerMaskBits;
	}

	void WriteLineRendererSettings(nlohmann::json& out,
		const Engine::LineRendererComponent& component) {

		out["material"] = Engine::ToAssetReferenceJson(component.material);
		out["materialInstance"] =
			Engine::WriteMaterialInstance(component.materialInstance);
		out["loop"] = component.loop;
		out["is2D"] = component.is2D;
		out["useWorldSpace"] = component.useWorldSpace;
		out["parentLocalFileID"] = component.parentLocalFileID.value;
		out["ignoreParentScale"] = component.ignoreParentScale;
		out["ignoreParentRotation"] = component.ignoreParentRotation;
		Engine::WriteRenderCommonFields(out, component.layer, component.order,
			component.visible, component.blendMode, component.queue);
		out["renderingLayerMask"] = component.renderingLayerMask &
			Engine::kRenderingLayerMaskBits;
	}

	std::vector<Engine::LinePoint> ReadLinePoints(const nlohmann::json& in) {

		std::vector<Engine::LinePoint> points{};
		const auto it = in.find("points");
		if (it == in.end() || !it->is_array()) {
			return points;
		}
		points.reserve(it->size());
		for (const nlohmann::json& pointJson : *it) {

			Engine::LinePoint point{};
			point.position = Engine::Vector3::FromJson(
				pointJson.value("position", nlohmann::json()));
			point.color = Engine::Color4::FromJson(
				pointJson.value("color", nlohmann::json()));
			point.thickness = pointJson.value("thickness", point.thickness);
			points.emplace_back(point);
		}
		return points;
	}
}

void Engine::LineRendererComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] LineRendererComponent& component) {

	if (!world.HasBuffer<LinePoint>(entity)) {
		world.AddBuffer<LinePoint>(entity);
	}
}

void Engine::LineRendererComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	if (world.HasBuffer<LinePoint>(entity)) {
		world.RemoveBuffer<LinePoint>(entity);
	}
}

void Engine::LineRendererComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] LineRendererComponent& component) {
}

void Engine::LineRendererComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] LineRendererComponent& component) {
}

void Engine::LineRendererComponent::DeserializeECS(
	ECSWorld& world, const Entity& entity, const nlohmann::json& in,
	LineRendererComponent& component) {

	ReadLineRendererSettings(in, component);
	const std::vector<LinePoint> points = ReadLinePoints(in);
	SetLinePoints(world, entity, points);
}

void Engine::LineRendererComponent::SerializeECS(
	const ECSWorld& world, const Entity& entity,
	const LineRendererComponent& component, nlohmann::json& out) {

	SerializeLineRenderer(component, GetLinePoints(world, entity), out);
}

void Engine::from_json(const nlohmann::json& in, LineRendererComponent& component) {

	ReadLineRendererSettings(in, component);
}

void Engine::to_json(nlohmann::json& out, const LineRendererComponent& component) {

	WriteLineRendererSettings(out, component);
	out["points"] = nlohmann::json::array();
}

std::span<Engine::LinePoint> Engine::GetLinePoints(
	ECSWorld& world, const Entity& entity) {

	return world.TryGetBuffer<LinePoint>(entity).GetSpan();
}

std::span<const Engine::LinePoint> Engine::GetLinePoints(
	const ECSWorld& world, const Entity& entity) {

	return world.GetBufferSpan<LinePoint>(entity);
}

void Engine::SetLinePoints(ECSWorld& world, const Entity& entity,
	std::span<const LinePoint> points) {

	DynamicBuffer<LinePoint> buffer = world.TryGetBuffer<LinePoint>(entity);
	if (!buffer.IsValid()) {
		buffer = world.AddBuffer<LinePoint>(entity);
	}
	buffer.Clear();
	buffer.Reserve(static_cast<uint32_t>(points.size()));
	for (const LinePoint& point : points) {
		buffer.Add(point);
	}
	world.MarkComponentModified<LinePoint>(entity);
}

void Engine::SerializeLineRenderer(const LineRendererComponent& component,
	std::span<const LinePoint> points, nlohmann::json& out) {

	WriteLineRendererSettings(out, component);
	out["points"] = nlohmann::json::array();
	for (const LinePoint& point : points) {

		nlohmann::json pointJson{};
		pointJson["position"] = point.position.ToJson();
		pointJson["color"] = point.color.ToJson();
		pointJson["thickness"] = point.thickness;
		out["points"].emplace_back(std::move(pointJson));
	}
}
