#include "CollisionComponent.h"

//============================================================================
//	CollisionComponent classMethods
//============================================================================

namespace {

	// jsonから衝突形状を読み込む
	Engine::CollisionShape LoadShape(const nlohmann::json& in) {

		Engine::CollisionShape shape{};
		if (!in.is_object()) {
			return shape;
		}

		shape.type = Engine::ColliderShapeTypeFromString(in.value("type", "Sphere3D"));
		shape.enabled = in.value("enabled", shape.enabled);
		shape.isTrigger = in.value("isTrigger", shape.isTrigger);
		shape.useTransformRotation = in.value("useTransformRotation", shape.useTransformRotation);
		shape.rotatedQuad = in.value("rotatedQuad", shape.rotatedQuad);
		if (in.contains("offset")) {
			shape.offset = Engine::Vector3::FromJson(in["offset"]);
		}
		if (in.contains("rotationDegrees")) {
			shape.rotationDegrees = Engine::Vector3::FromJson(in["rotationDegrees"]);
		}
		shape.radius = in.value("radius", shape.radius);
		if (in.contains("halfSize2D")) {
			shape.halfSize2D = Engine::Vector2::FromJson(in["halfSize2D"]);
		}
		if (in.contains("halfExtents3D")) {
			shape.halfExtents3D = Engine::Vector3::FromJson(in["halfExtents3D"]);
		}
		return shape;
	}

	// 衝突形状をjsonへ書き出す
	nlohmann::json SaveShape(const Engine::CollisionShape& shape) {

		nlohmann::json out = nlohmann::json::object();
		out["type"] = Engine::ToString(shape.type);
		out["enabled"] = shape.enabled;
		out["isTrigger"] = shape.isTrigger;
		out["useTransformRotation"] = shape.useTransformRotation;
		out["rotatedQuad"] = shape.rotatedQuad;
		out["offset"] = shape.offset.ToJson();
		out["rotationDegrees"] = shape.rotationDegrees.ToJson();
		out["radius"] = shape.radius;
		out["halfSize2D"] = shape.halfSize2D.ToJson();
		out["halfExtents3D"] = shape.halfExtents3D.ToJson();
		return out;
	}
}

void Engine::from_json(const nlohmann::json& in, CollisionComponent& component) {

	// CollisionComponent本体を読み込む
	component.enabled = in.value("enabled", component.enabled);
	component.isStatic = in.value("isStatic", component.isStatic);
	component.enablePushback = in.value("enablePushback", component.enablePushback);
	component.typeMask = in.value("typeMask", component.typeMask);

	// 形状がない場合はデフォルト形状を1つ持たせる
	component.shapes.clear();
	if (in.contains("shapes") && in["shapes"].is_array()) {
		for (const auto& shapeJson : in["shapes"]) {
			component.shapes.push_back(LoadShape(shapeJson));
		}
	}
	if (component.shapes.empty()) {
		component.shapes.push_back(CollisionShape{});
	}
}

void Engine::to_json(nlohmann::json& out, const CollisionComponent& component) {

	// CollisionComponent本体を書き出す
	out["enabled"] = component.enabled;
	out["isStatic"] = component.isStatic;
	out["enablePushback"] = component.enablePushback;
	out["typeMask"] = component.typeMask;

	// 設定されている形状を書き出す
	out["shapes"] = nlohmann::json::array();
	for (const auto& shape : component.shapes) {
		out["shapes"].push_back(SaveShape(shape));
	}
}
