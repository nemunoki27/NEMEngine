#include "UVTransformComponent.h"

//============================================================================
//	UVTransformComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, UVTransformComponent& component) {

	component.pos = Vector2::FromJson(in.value("pos", nlohmann::json{}));
	component.rotation = in.value("rotation", 0.0f);
	component.scale = Vector2::FromJson(in.value("scale", nlohmann::json{}));

	// ワールド行列と変更検知フラグはシリアライズされないため、初期化しておく
	component.uvMatrix = Matrix4x4::Identity();
}

void Engine::to_json(nlohmann::json& out, const UVTransformComponent& component) {

	out["pos"] = component.pos.ToJson();
	out["rotation"] = component.rotation;
	out["scale"] = component.scale.ToJson();
}