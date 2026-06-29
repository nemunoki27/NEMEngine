#include "FillFaceMeshRendererComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	FillFaceMeshRendererComponent structMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, FillMeshRendererComponent& component) {

	component.buildMesh = in.value("buildMesh", component.buildMesh);
	component.material = ParseAssetID(in, "material");
	ReadMaterialParameterOverrides(in.value("parameterOverrides", nlohmann::json::object()), component.parameterOverrides);

	// 面の頂点列はVector3配列、保存が無ければ空のまま
	component.facePositions.clear();
	if (const auto it = in.find("facePositions"); it != in.end() && it->is_array()) {

		component.facePositions.reserve(it->size());
		for (const nlohmann::json& point : *it) {
			component.facePositions.push_back(Vector3::FromJson(point));
		}
	}

	component.color = Color4::FromJson(in.value("color", nlohmann::json()));
	component.queue = RenderPhaseFromString(in.value("queue", std::string(ToString(component.queue))), component.queue);
	component.layer = in.value("layer", component.layer);
	component.order = in.value("order", component.order);
	component.visible = in.value("visible", component.visible);
	component.blendMode = EnumAdapter<BlendMode>::FromString(in.value("blendMode", "Normal")).value();
}

void Engine::to_json(nlohmann::json& out, const FillMeshRendererComponent& component) {

	out["buildMesh"] = component.buildMesh;
	out["material"] = ToAssetReferenceJson(component.material);
	out["parameterOverrides"] = WriteMaterialParameterOverrides(component.parameterOverrides);

	nlohmann::json positions = nlohmann::json::array();
	for (const Vector3& point : component.facePositions) {
		positions.push_back(point.ToJson());
	}
	out["facePositions"] = std::move(positions);

	out["color"] = component.color.ToJson();
	out["queue"] = std::string(ToString(component.queue));
	out["layer"] = component.layer;
	out["order"] = component.order;
	out["visible"] = component.visible;
	out["blendMode"] = EnumAdapter<BlendMode>::ToString(component.blendMode);
}
