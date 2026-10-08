#include "JsonMath.h"

//============================================================================
//	include
//============================================================================
#include <cmath>
#include <limits>

using namespace Engine;

namespace {

	// floatで表現できる有限値だけを取得する
	bool TryGetFiniteFloat(const nlohmann::json& object, const char* key, float& outValue) {

		const auto it = object.find(key);
		if (it == object.end() || !it->is_number()) {
			return false;
		}

		const double value = it->get<double>();
		if (!std::isfinite(value) || value < -(std::numeric_limits<float>::max)() ||
			value > (std::numeric_limits<float>::max)()) {
			return false;
		}
		outValue = static_cast<float>(value);
		return true;
	}
}

void JsonMath::SetVector2(nlohmann::json& json, const std::string& key, const Vector2& value) {

	// 数学型の保存形式を再利用
	json[key] = value.ToJson();
}

Vector2 JsonMath::GetVector2(const nlohmann::json& json, const std::string& key, const Vector2& defaultValue) {

	// 有効な全成分が揃う場合だけ取得
	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Vector2 value{};
	return TryGetFiniteFloat(*it, "x", value.x) && TryGetFiniteFloat(*it, "y", value.y) ? value : defaultValue;
}

void JsonMath::SetVector3(nlohmann::json& json, const std::string& key, const Vector3& value) {

	// 数学型の保存形式を再利用
	json[key] = value.ToJson();
}

Vector3 JsonMath::GetVector3(const nlohmann::json& json, const std::string& key, const Vector3& defaultValue) {

	// 有効な全成分が揃う場合だけ取得
	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Vector3 value{};
	return TryGetFiniteFloat(*it, "x", value.x) && TryGetFiniteFloat(*it, "y", value.y) && TryGetFiniteFloat(*it, "z", value.z)
			   ? value
			   : defaultValue;
}

void JsonMath::SetVector4(nlohmann::json& json, const std::string& key, const Vector4& value) {

	// 数学型の保存形式を再利用
	json[key] = value.ToJson();
}

Vector4 JsonMath::GetVector4(const nlohmann::json& json, const std::string& key, const Vector4& defaultValue) {

	// 有効な全成分が揃う場合だけ取得
	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Vector4 value{};
	return TryGetFiniteFloat(*it, "x", value.x) && TryGetFiniteFloat(*it, "y", value.y) &&
				   TryGetFiniteFloat(*it, "z", value.z) && TryGetFiniteFloat(*it, "w", value.w)
			   ? value
			   : defaultValue;
}

void JsonMath::SetQuaternion(nlohmann::json& json, const std::string& key, const Quaternion& value) {

	// 数学型の保存形式を再利用
	json[key] = value.ToJson();
}

Quaternion JsonMath::GetQuaternion(const nlohmann::json& json, const std::string& key, const Quaternion& defaultValue) {

	// 有効な全成分が揃う場合だけ取得
	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Quaternion value{};
	return TryGetFiniteFloat(*it, "x", value.x) && TryGetFiniteFloat(*it, "y", value.y) &&
				   TryGetFiniteFloat(*it, "z", value.z) && TryGetFiniteFloat(*it, "w", value.w)
			   ? value
			   : defaultValue;
}

void JsonMath::SetColor3(nlohmann::json& json, const std::string& key, const Color3& value) {

	// 数学型の保存形式を再利用
	json[key] = value.ToJson();
}

Color3 JsonMath::GetColor3(const nlohmann::json& json, const std::string& key, const Color3& defaultValue) {

	// 有効な全成分が揃う場合だけ取得
	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Color3 value{};
	return TryGetFiniteFloat(*it, "r", value.r) && TryGetFiniteFloat(*it, "g", value.g) && TryGetFiniteFloat(*it, "b", value.b)
			   ? value
			   : defaultValue;
}

void JsonMath::SetColor4(nlohmann::json& json, const std::string& key, const Color4& value) {

	// 数学型の保存形式を再利用
	json[key] = value.ToJson();
}

Color4 JsonMath::GetColor4(const nlohmann::json& json, const std::string& key, const Color4& defaultValue) {

	// 有効な全成分が揃う場合だけ取得
	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Color4 value{};
	return TryGetFiniteFloat(*it, "r", value.r) && TryGetFiniteFloat(*it, "g", value.g) &&
				   TryGetFiniteFloat(*it, "b", value.b) && TryGetFiniteFloat(*it, "a", value.a)
			   ? value
			   : defaultValue;
}
