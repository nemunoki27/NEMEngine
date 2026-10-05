#include "MaterialAnimationPropertyValue.h"

//============================================================================
//	include
//============================================================================
// c++
#include <variant>

Engine::AnimationPropertyValue Engine::ZeroAnimationValue(Engine::AnimationValueType type) {

	// Trackの型に合わせて初期値を作る
	switch (type) {
	case Engine::AnimationValueType::Vector2:
		return Engine::Vector2{};
	case Engine::AnimationValueType::Vector3:
		return Engine::Vector3{};
	case Engine::AnimationValueType::Vector4:
		return Engine::Vector4{};
	case Engine::AnimationValueType::Color3:
		return Engine::Color3{};
	case Engine::AnimationValueType::Color4:
		return Engine::Color4{};
	default:
		return 0.0f;
	}
}

bool Engine::MaterialValueToAnimation(
	const Engine::MaterialParameterValue& value, Engine::AnimationValueType type, Engine::AnimationPropertyValue& out) {

	// Trackの型に合う保存値を取得する
	switch (type) {
	case Engine::AnimationValueType::Float:
		if (const float* v = std::get_if<float>(&value.value)) {
			out = *v;
			return true;
		}
		break;
	case Engine::AnimationValueType::Vector2:
		if (const Engine::Vector2* v = std::get_if<Engine::Vector2>(&value.value)) {
			out = *v;
			return true;
		}
		break;
	case Engine::AnimationValueType::Vector3:
		if (const Engine::Vector3* v = std::get_if<Engine::Vector3>(&value.value)) {
			out = *v;
			return true;
		}
		break;
	case Engine::AnimationValueType::Vector4:
		if (const Engine::Vector4* v = std::get_if<Engine::Vector4>(&value.value)) {
			out = *v;
			return true;
		}
		break;
	case Engine::AnimationValueType::Color3:
		if (const Engine::Vector3* v = std::get_if<Engine::Vector3>(&value.value)) {
			out = Engine::Color3(v->x, v->y, v->z);
			return true;
		}
		if (const Engine::Color4* v = std::get_if<Engine::Color4>(&value.value)) {
			out = Engine::Color3(v->r, v->g, v->b);
			return true;
		}
		break;
	case Engine::AnimationValueType::Color4:
		if (const Engine::Color4* v = std::get_if<Engine::Color4>(&value.value)) {
			out = *v;
			return true;
		}
		if (const Engine::Vector4* v = std::get_if<Engine::Vector4>(&value.value)) {
			out = Engine::Color4(v->x, v->y, v->z, v->w);
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}

Engine::MaterialParameterValue Engine::AnimationValueToMaterial(const Engine::AnimationPropertyValue& value) {

	// Materialで保存する型へ揃える
	Engine::MaterialParameterValue result{};
	if (const float* v = std::get_if<float>(&value)) {
		result.value = *v;
		return result;
	}
	if (const Engine::Vector2* v = std::get_if<Engine::Vector2>(&value)) {
		result.value = *v;
		return result;
	}
	if (const Engine::Vector3* v = std::get_if<Engine::Vector3>(&value)) {
		result.value = *v;
		return result;
	}
	if (const Engine::Vector4* v = std::get_if<Engine::Vector4>(&value)) {
		result.value = *v;
		return result;
	}
	// 3成分の色はVector3として保持する
	if (const Engine::Color3* v = std::get_if<Engine::Color3>(&value)) {
		result.value = Engine::Vector3(v->r, v->g, v->b);
		return result;
	}
	if (const Engine::Color4* v = std::get_if<Engine::Color4>(&value)) {
		result.value = *v;
		return result;
	}
	return result;
}
