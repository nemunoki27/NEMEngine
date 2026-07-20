#include "PrimitiveRendererComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>

//============================================================================
//	PrimitiveRendererComponent structMethods
//============================================================================
void Engine::to_json(nlohmann::json& out, const PrimitivePlaneParams& params) {

	out["size"] = params.size.ToJson();
	out["pivot"] = params.pivot.ToJson();
	out["axis"] = EnumAdapter<PrimitivePlaneAxis>::ToString(params.axis);
	out["divideX"] = params.divideX;
	out["divideY"] = params.divideY;
}

void Engine::from_json(const nlohmann::json& in, PrimitivePlaneParams& params) {

	params.size = Vector2::FromJson(in.value("size", nlohmann::json()));
	params.pivot = Vector2::FromJson(in.value("pivot", nlohmann::json()));
	params.axis = EnumAdapter<PrimitivePlaneAxis>::FromString(
		in.value("axis", "XY")).value_or(PrimitivePlaneAxis::XY);
	params.divideX = in.value("divideX", params.divideX);
	params.divideY = in.value("divideY", params.divideY);
}

void Engine::to_json(nlohmann::json& out, const PrimitiveCrossPlaneParams& params) {

	out["size"] = params.size.ToJson();
	out["pivot"] = params.pivot.ToJson();
	out["planeCount"] = params.planeCount;
}

void Engine::from_json(const nlohmann::json& in, PrimitiveCrossPlaneParams& params) {

	params.size = Vector2::FromJson(in.value("size", nlohmann::json()));
	params.pivot = Vector2::FromJson(in.value("pivot", nlohmann::json()));
	params.planeCount = in.value("planeCount", params.planeCount);
}

void Engine::to_json(nlohmann::json& out, const PrimitiveRingParams& params) {

	out["outerRadius"] = params.outerRadius;
	out["innerRadius"] = params.innerRadius;
	out["startAngle"] = params.startAngle;
	out["endAngle"] = params.endAngle;
	out["divide"] = params.divide;
}

void Engine::from_json(const nlohmann::json& in, PrimitiveRingParams& params) {

	params.outerRadius = in.value("outerRadius", params.outerRadius);
	params.innerRadius = in.value("innerRadius", params.innerRadius);
	params.startAngle = in.value("startAngle", params.startAngle);
	params.endAngle = in.value("endAngle", params.endAngle);
	params.divide = in.value("divide", params.divide);
}

void Engine::to_json(nlohmann::json& out, const PrimitiveCylinderParams& params) {

	out["topRadius"] = params.topRadius;
	out["centerRadius"] = params.centerRadius;
	out["bottomRadius"] = params.bottomRadius;
	out["topRadiusWeight"] = params.topRadiusWeight;
	out["bottomRadiusWeight"] = params.bottomRadiusWeight;
	out["topColor"] = params.topColor.ToJson();
	out["centerColor"] = params.centerColor.ToJson();
	out["bottomColor"] = params.bottomColor.ToJson();
	out["height"] = params.height;
	out["maxAngleDegrees"] = params.maxAngle;
	out["radialDivide"] = params.radialDivide;
	out["heightDivide"] = params.heightDivide;
	out["cap"] = EnumAdapter<PrimitiveCylinderCap>::ToString(params.cap);
	out["uvMode"] = EnumAdapter<PrimitiveCylinderUVMode>::ToString(params.uvMode);
}

void Engine::from_json(const nlohmann::json& in, PrimitiveCylinderParams& params) {

	params.topRadius = in.value("topRadius", params.topRadius);
	params.bottomRadius = in.value("bottomRadius", params.bottomRadius);
	params.centerRadius = in.value("centerRadius", (params.topRadius + params.bottomRadius) * 0.5f);
	params.topRadiusWeight = std::clamp(in.value("topRadiusWeight", params.topRadiusWeight), 0.0f, 1.0f);
	params.bottomRadiusWeight = std::clamp(in.value("bottomRadiusWeight", params.bottomRadiusWeight), 0.0f, 1.0f);
	if (const auto it = in.find("topColor"); it != in.end()) { params.topColor = Color4::FromJson(*it); }
	if (const auto it = in.find("centerColor"); it != in.end()) { params.centerColor = Color4::FromJson(*it); }
	if (const auto it = in.find("bottomColor"); it != in.end()) { params.bottomColor = Color4::FromJson(*it); }
	params.height = in.value("height", params.height);
	if (const auto it = in.find("maxAngleDegrees"); it != in.end()) {
		params.maxAngle = it->get<float>();
	}
	if (const auto it = in.find("maxAngle"); it != in.end()) {
		params.maxAngle = it->get<float>() * (180.0f / std::numbers::pi_v<float>);
	}
	params.radialDivide = in.value("radialDivide", params.radialDivide);
	params.heightDivide = (std::max)(2, in.value("heightDivide", params.heightDivide));
	params.cap = EnumAdapter<PrimitiveCylinderCap>::FromString(
		in.value("cap", "Both")).value_or(PrimitiveCylinderCap::Both);
	params.uvMode = EnumAdapter<PrimitiveCylinderUVMode>::FromString(
		in.value("uvMode", "None")).value_or(PrimitiveCylinderUVMode::None);
}

void Engine::to_json(nlohmann::json& out, const PrimitiveSphereParams& params) {

	out["radius"] = params.radius;
	out["longitudeDivide"] = params.longitudeDivide;
	out["latitudeDivide"] = params.latitudeDivide;
}

void Engine::from_json(const nlohmann::json& in, PrimitiveSphereParams& params) {

	params.radius = in.value("radius", params.radius);
	params.longitudeDivide = in.value("longitudeDivide", params.longitudeDivide);
	params.latitudeDivide = in.value("latitudeDivide", params.latitudeDivide);
}

void Engine::to_json(nlohmann::json& out, const PrimitiveHemisphereParams& params) {

	out["radius"] = params.radius;
	out["longitudeDivide"] = params.longitudeDivide;
	out["latitudeDivide"] = params.latitudeDivide;
	out["bottomCap"] = params.bottomCap;
}

void Engine::from_json(const nlohmann::json& in, PrimitiveHemisphereParams& params) {

	params.radius = in.value("radius", params.radius);
	params.longitudeDivide = in.value("longitudeDivide", params.longitudeDivide);
	params.latitudeDivide = in.value("latitudeDivide", params.latitudeDivide);
	params.bottomCap = in.value("bottomCap", params.bottomCap);
}

void Engine::to_json(nlohmann::json& out, const PrimitiveCubeParams& params) {

	out["size"] = params.size.ToJson();
	out["pivot"] = params.pivot.ToJson();
}

void Engine::from_json(const nlohmann::json& in, PrimitiveCubeParams& params) {

	params.size = Vector3::FromJson(in.value("size", nlohmann::json()));
	params.pivot = Vector3::FromJson(in.value("pivot", nlohmann::json()));
}

void Engine::from_json(const nlohmann::json& in, PrimitiveRendererComponent& component) {

	component.type = EnumAdapter<PrimitiveType>::FromString(
		in.value("type", "Plane")).value_or(PrimitiveType::Plane);
	component.renderSpace = EnumAdapter<PrimitiveRenderSpace>::FromString(
		in.value("renderSpace", "World3D")).value_or(PrimitiveRenderSpace::World3D);

	if (const auto it = in.find("plane"); it != in.end()) { from_json(*it, component.plane); }
	if (const auto it = in.find("crossPlane"); it != in.end()) { from_json(*it, component.crossPlane); }
	if (const auto it = in.find("ring"); it != in.end()) { from_json(*it, component.ring); }
	if (const auto it = in.find("cylinder"); it != in.end()) { from_json(*it, component.cylinder); }
	if (const auto it = in.find("sphere"); it != in.end()) { from_json(*it, component.sphere); }
	if (const auto it = in.find("hemisphere"); it != in.end()) { from_json(*it, component.hemisphere); }
	if (const auto it = in.find("cube"); it != in.end()) { from_json(*it, component.cube); }

	component.material = ParseAssetID(in, "material");
	ReadMaterialParameterOverrides(in.value("parameterOverrides", nlohmann::json::object()), component.parameterOverrides);

	ReadRenderCommonFields(in, component.layer, component.order, component.visible, component.blendMode, component.queue);
	ReadMeshRenderFlags(in, component.renderFlags);
}

void Engine::to_json(nlohmann::json& out, const PrimitiveRendererComponent& component) {

	out["type"] = EnumAdapter<PrimitiveType>::ToString(component.type);
	out["renderSpace"] = EnumAdapter<PrimitiveRenderSpace>::ToString(component.renderSpace);

	out["plane"] = component.plane;
	out["crossPlane"] = component.crossPlane;
	out["ring"] = component.ring;
	out["cylinder"] = component.cylinder;
	out["sphere"] = component.sphere;
	out["hemisphere"] = component.hemisphere;
	out["cube"] = component.cube;

	out["material"] = ToAssetReferenceJson(component.material);
	out["parameterOverrides"] = WriteMaterialParameterOverrides(component.parameterOverrides);

	WriteRenderCommonFields(out, component.layer, component.order, component.visible, component.blendMode, component.queue);
	WriteMeshRenderFlags(out, component.renderFlags);
}
