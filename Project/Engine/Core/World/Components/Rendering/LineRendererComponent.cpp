#include "LineRendererComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	LineRendererComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, LineRendererComponent& component) {

	component.material = ParseAssetID(in, "material");
	ReadMaterialParameterOverrides(in.value("parameterOverrides", nlohmann::json::object()), component.parameterOverrides);

	// 点列を読み込む、未指定や型違いは空のまま
	component.points.clear();
	if (const auto it = in.find("points"); it != in.end() && it->is_array()) {

		component.points.reserve(it->size());
		for (const auto& pointJson : *it) {

			LinePoint point{};
			point.position = Vector3::FromJson(pointJson.value("position", nlohmann::json()));
			point.color = Color4::FromJson(pointJson.value("color", nlohmann::json()));
			point.thickness = pointJson.value("thickness", point.thickness);
			component.points.emplace_back(point);
		}
	}

	component.loop = in.value("loop", component.loop);
	component.is2D = in.value("is2D", component.is2D);
	component.useWorldSpace = in.value("useWorldSpace", component.useWorldSpace);
	component.parentLocalFileID.value = in.value("parentLocalFileID", component.parentLocalFileID.value);
	component.ignoreParentScale = in.value("ignoreParentScale", component.ignoreParentScale);
	component.ignoreParentRotation = in.value("ignoreParentRotation", component.ignoreParentRotation);

	component.queue = RenderPhaseFromString(in.value("queue", std::string(ToString(component.queue))), component.queue);
	component.layer = in.value("layer", component.layer);
	component.order = in.value("order", component.order);
	component.visible = in.value("visible", component.visible);
	component.blendMode = EnumAdapter<BlendMode>::FromString(in.value("blendMode", "Normal")).value();
}

void Engine::to_json(nlohmann::json& out, const LineRendererComponent& component) {

	out["material"] = ToAssetReferenceJson(component.material);
	out["parameterOverrides"] = WriteMaterialParameterOverrides(component.parameterOverrides);

	// 点列を書き出す
	nlohmann::json pointsJson = nlohmann::json::array();
	for (const auto& point : component.points) {

		nlohmann::json pointJson{};
		pointJson["position"] = point.position.ToJson();
		pointJson["color"] = point.color.ToJson();
		pointJson["thickness"] = point.thickness;
		pointsJson.emplace_back(std::move(pointJson));
	}
	out["points"] = std::move(pointsJson);

	out["loop"] = component.loop;
	out["is2D"] = component.is2D;
	out["useWorldSpace"] = component.useWorldSpace;
	out["parentLocalFileID"] = component.parentLocalFileID.value;
	out["ignoreParentScale"] = component.ignoreParentScale;
	out["ignoreParentRotation"] = component.ignoreParentRotation;

	out["queue"] = std::string(ToString(component.queue));
	out["layer"] = component.layer;
	out["order"] = component.order;
	out["visible"] = component.visible;
	out["blendMode"] = EnumAdapter<BlendMode>::ToString(component.blendMode);
}
