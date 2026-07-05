#include "PrimitiveRendererComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	PrimitiveRendererComponent structMethods
//============================================================================
namespace {

	nlohmann::json SavePlane(const Engine::PrimitivePlaneParams& params) {

		nlohmann::json out;
		out["size"] = params.size.ToJson();
		out["pivot"] = params.pivot.ToJson();
		out["axis"] = Engine::EnumAdapter<Engine::PrimitivePlaneAxis>::ToString(params.axis);
		out["divideX"] = params.divideX;
		out["divideY"] = params.divideY;
		return out;
	}

	Engine::PrimitivePlaneParams LoadPlane(const nlohmann::json& in) {

		Engine::PrimitivePlaneParams params{};
		params.size = Engine::Vector2::FromJson(in.value("size", nlohmann::json()));
		params.pivot = Engine::Vector2::FromJson(in.value("pivot", nlohmann::json()));
		params.axis = Engine::EnumAdapter<Engine::PrimitivePlaneAxis>::FromString(
			in.value("axis", "XY")).value_or(Engine::PrimitivePlaneAxis::XY);
		params.divideX = in.value("divideX", params.divideX);
		params.divideY = in.value("divideY", params.divideY);
		return params;
	}

	nlohmann::json SaveCrossPlane(const Engine::PrimitiveCrossPlaneParams& params) {

		nlohmann::json out;
		out["size"] = params.size.ToJson();
		out["pivot"] = params.pivot.ToJson();
		out["planeCount"] = params.planeCount;
		return out;
	}

	Engine::PrimitiveCrossPlaneParams LoadCrossPlane(const nlohmann::json& in) {

		Engine::PrimitiveCrossPlaneParams params{};
		params.size = Engine::Vector2::FromJson(in.value("size", nlohmann::json()));
		params.pivot = Engine::Vector2::FromJson(in.value("pivot", nlohmann::json()));
		params.planeCount = in.value("planeCount", params.planeCount);
		return params;
	}

	nlohmann::json SaveRing(const Engine::PrimitiveRingParams& params) {

		nlohmann::json out;
		out["outerRadius"] = params.outerRadius;
		out["innerRadius"] = params.innerRadius;
		out["divide"] = params.divide;
		return out;
	}

	Engine::PrimitiveRingParams LoadRing(const nlohmann::json& in) {

		Engine::PrimitiveRingParams params{};
		params.outerRadius = in.value("outerRadius", params.outerRadius);
		params.innerRadius = in.value("innerRadius", params.innerRadius);
		params.divide = in.value("divide", params.divide);
		return params;
	}

	nlohmann::json SaveCylinder(const Engine::PrimitiveCylinderParams& params) {

		nlohmann::json out;
		out["topRadius"] = params.topRadius;
		out["bottomRadius"] = params.bottomRadius;
		out["height"] = params.height;
		out["maxAngle"] = params.maxAngle;
		out["radialDivide"] = params.radialDivide;
		out["heightDivide"] = params.heightDivide;
		out["cap"] = Engine::EnumAdapter<Engine::PrimitiveCylinderCap>::ToString(params.cap);
		out["uvMode"] = Engine::EnumAdapter<Engine::PrimitiveCylinderUVMode>::ToString(params.uvMode);
		return out;
	}

	Engine::PrimitiveCylinderParams LoadCylinder(const nlohmann::json& in) {

		Engine::PrimitiveCylinderParams params{};
		params.topRadius = in.value("topRadius", params.topRadius);
		params.bottomRadius = in.value("bottomRadius", params.bottomRadius);
		params.height = in.value("height", params.height);
		params.maxAngle = in.value("maxAngle", params.maxAngle);
		params.radialDivide = in.value("radialDivide", params.radialDivide);
		params.heightDivide = in.value("heightDivide", params.heightDivide);
		params.cap = Engine::EnumAdapter<Engine::PrimitiveCylinderCap>::FromString(
			in.value("cap", "Both")).value_or(Engine::PrimitiveCylinderCap::Both);
		params.uvMode = Engine::EnumAdapter<Engine::PrimitiveCylinderUVMode>::FromString(
			in.value("uvMode", "None")).value_or(Engine::PrimitiveCylinderUVMode::None);
		return params;
	}

	nlohmann::json SaveSphere(const Engine::PrimitiveSphereParams& params) {

		nlohmann::json out;
		out["radius"] = params.radius;
		out["longitudeDivide"] = params.longitudeDivide;
		out["latitudeDivide"] = params.latitudeDivide;
		return out;
	}

	Engine::PrimitiveSphereParams LoadSphere(const nlohmann::json& in) {

		Engine::PrimitiveSphereParams params{};
		params.radius = in.value("radius", params.radius);
		params.longitudeDivide = in.value("longitudeDivide", params.longitudeDivide);
		params.latitudeDivide = in.value("latitudeDivide", params.latitudeDivide);
		return params;
	}

	nlohmann::json SaveHemisphere(const Engine::PrimitiveHemisphereParams& params) {

		nlohmann::json out;
		out["radius"] = params.radius;
		out["longitudeDivide"] = params.longitudeDivide;
		out["latitudeDivide"] = params.latitudeDivide;
		out["bottomCap"] = params.bottomCap;
		return out;
	}

	Engine::PrimitiveHemisphereParams LoadHemisphere(const nlohmann::json& in) {

		Engine::PrimitiveHemisphereParams params{};
		params.radius = in.value("radius", params.radius);
		params.longitudeDivide = in.value("longitudeDivide", params.longitudeDivide);
		params.latitudeDivide = in.value("latitudeDivide", params.latitudeDivide);
		params.bottomCap = in.value("bottomCap", params.bottomCap);
		return params;
	}

	nlohmann::json SaveCube(const Engine::PrimitiveCubeParams& params) {

		nlohmann::json out;
		out["size"] = params.size.ToJson();
		out["pivot"] = params.pivot.ToJson();
		return out;
	}

	Engine::PrimitiveCubeParams LoadCube(const nlohmann::json& in) {

		Engine::PrimitiveCubeParams params{};
		params.size = Engine::Vector3::FromJson(in.value("size", nlohmann::json()));
		params.pivot = Engine::Vector3::FromJson(in.value("pivot", nlohmann::json()));
		return params;
	}
}

void Engine::from_json(const nlohmann::json& in, PrimitiveRendererComponent& component) {

	component.type = EnumAdapter<PrimitiveType>::FromString(
		in.value("type", "Plane")).value_or(PrimitiveType::Plane);

	if (const auto it = in.find("plane"); it != in.end()) { component.plane = LoadPlane(*it); }
	if (const auto it = in.find("crossPlane"); it != in.end()) { component.crossPlane = LoadCrossPlane(*it); }
	if (const auto it = in.find("ring"); it != in.end()) { component.ring = LoadRing(*it); }
	if (const auto it = in.find("cylinder"); it != in.end()) { component.cylinder = LoadCylinder(*it); }
	if (const auto it = in.find("sphere"); it != in.end()) { component.sphere = LoadSphere(*it); }
	if (const auto it = in.find("hemisphere"); it != in.end()) { component.hemisphere = LoadHemisphere(*it); }
	if (const auto it = in.find("cube"); it != in.end()) { component.cube = LoadCube(*it); }

	component.material = ParseAssetID(in, "material");
	ReadMaterialParameterOverrides(in.value("parameterOverrides", nlohmann::json::object()), component.parameterOverrides);

	component.queue = RenderPhaseFromString(in.value("queue", std::string(ToString(component.queue))), component.queue);
	component.layer = in.value("layer", component.layer);
	component.order = in.value("order", component.order);
	component.visible = in.value("visible", component.visible);
	component.blendMode = EnumAdapter<BlendMode>::FromString(in.value("blendMode", "Normal")).value();
	component.renderFlags = static_cast<MeshRenderFlags>(
		in.value("renderFlags", static_cast<uint32_t>(component.renderFlags)));
}

void Engine::to_json(nlohmann::json& out, const PrimitiveRendererComponent& component) {

	out["type"] = EnumAdapter<PrimitiveType>::ToString(component.type);

	out["plane"] = SavePlane(component.plane);
	out["crossPlane"] = SaveCrossPlane(component.crossPlane);
	out["ring"] = SaveRing(component.ring);
	out["cylinder"] = SaveCylinder(component.cylinder);
	out["sphere"] = SaveSphere(component.sphere);
	out["hemisphere"] = SaveHemisphere(component.hemisphere);
	out["cube"] = SaveCube(component.cube);

	out["material"] = ToAssetReferenceJson(component.material);
	out["parameterOverrides"] = WriteMaterialParameterOverrides(component.parameterOverrides);

	out["queue"] = std::string(ToString(component.queue));
	out["layer"] = component.layer;
	out["order"] = component.order;
	out["visible"] = component.visible;
	out["blendMode"] = EnumAdapter<BlendMode>::ToString(component.blendMode);
	out["renderFlags"] = static_cast<uint32_t>(component.renderFlags);
}
