#include "UVTransformComponent.h"

//============================================================================
//	UVTransformComponent classMethods
//============================================================================
Engine::Matrix4x4 Engine::MakeUVMatrix(const UVTransformComponent& component) {

	return Matrix4x4::MakeAffineMatrix({component.scale.x, component.scale.y, 1.0f},
		Vector3{0.0f, 0.0f, component.rotation}, {component.pos.x, component.pos.y, 0.0f});
}

void Engine::from_json(const nlohmann::json& in, UVTransformComponent& component) {

	component.pos = Vector2::FromJson(in.value("pos", nlohmann::json{}));
	component.rotation = in.value("rotation", 0.0f);
	component.scale = Vector2::FromJson(in.value("scale", Vector2::AnyInit(1.0f).ToJson()));

	// 読み込んだ値からUV行列と変更検知用の値を復元する
	component.uvMatrix = MakeUVMatrix(component);
	component.prePos = component.pos;
	component.preScale = component.scale;
	component.preRotation = component.rotation;
}

void Engine::to_json(nlohmann::json& out, const UVTransformComponent& component) {

	out["pos"] = component.pos.ToJson();
	out["rotation"] = component.rotation;
	out["scale"] = component.scale.ToJson();
}