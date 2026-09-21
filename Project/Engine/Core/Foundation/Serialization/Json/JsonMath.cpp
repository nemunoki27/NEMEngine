#include "JsonMath.h"

//============================================================================
//	include
//============================================================================
#include <cmath>
#include <limits>

using namespace Engine;

namespace {

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
	// Vector2を{x, y}オブジェクトとしてセット
	json[key] = { {"x", value.x}, {"y", value.y} };
}

Vector2 JsonMath::GetVector2(const nlohmann::json& json, const std::string& key, const Vector2& defaultValue) {

	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Vector2 value{};
	return TryGetFiniteFloat(*it, "x", value.x) && TryGetFiniteFloat(*it, "y", value.y) ?
	value : defaultValue;
}

void JsonMath::SetVector3(nlohmann::json& json, const std::string& key, const Vector3& value) {
	json[key] = { {"x", value.x}, {"y", value.y}, {"z", value.z} };
}

Vector3 JsonMath::GetVector3(const nlohmann::json& json, const std::string& key, const Vector3& defaultValue) {

	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Vector3 value{};
	return TryGetFiniteFloat(*it, "x", value.x) && TryGetFiniteFloat(*it, "y", value.y) &&
	TryGetFiniteFloat(*it, "z", value.z) ? value : defaultValue;
}

void JsonMath::SetVector4(nlohmann::json& json, const std::string& key, const Vector4& value) {
	json[key] = { {"x", value.x}, {"y", value.y}, {"z", value.z}, {"w", value.w} };
}

Vector4 JsonMath::GetVector4(const nlohmann::json& json, const std::string& key, const Vector4& defaultValue) {

	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Vector4 value{};
	return TryGetFiniteFloat(*it, "x", value.x) && TryGetFiniteFloat(*it, "y", value.y) &&
	TryGetFiniteFloat(*it, "z", value.z) && TryGetFiniteFloat(*it, "w", value.w) ?
	value : defaultValue;
}

void JsonMath::SetQuaternion(nlohmann::json& json, const std::string& key, const Quaternion& value) {
	// クォータニオンを{x, y, z, w}オブジェクトとして保存
	json[key] = { {"x", value.x}, {"y", value.y}, {"z", value.z}, {"w", value.w} };
}

Quaternion JsonMath::GetQuaternion(const nlohmann::json& json, const std::string& key, const Quaternion& defaultValue) {

	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Quaternion value{};
	return TryGetFiniteFloat(*it, "x", value.x) && TryGetFiniteFloat(*it, "y", value.y) &&
	TryGetFiniteFloat(*it, "z", value.z) && TryGetFiniteFloat(*it, "w", value.w) ?
	value : defaultValue;
}

void JsonMath::SetColor3(nlohmann::json& json, const std::string& key, const Color3& value) {
	// Color3を{r, g, b}オブジェクトとしてセット
	json[key] = { {"r", value.r}, {"g", value.g}, {"b", value.b} };
}

Color3 JsonMath::GetColor3(const nlohmann::json& json, const std::string& key, const Color3& defaultValue) {

	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Color3 value{};
	return TryGetFiniteFloat(*it, "r", value.r) && TryGetFiniteFloat(*it, "g", value.g) &&
	TryGetFiniteFloat(*it, "b", value.b) ? value : defaultValue;
}

void JsonMath::SetColor4(nlohmann::json& json, const std::string& key, const Color4& value) {
	// Color4を{r, g, b, a}オブジェクトとしてセット
	json[key] = { {"r", value.r}, {"g", value.g}, {"b", value.b}, {"a", value.a} };
}

Color4 JsonMath::GetColor4(const nlohmann::json& json, const std::string& key, const Color4& defaultValue) {

	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Color4 value{};
	return TryGetFiniteFloat(*it, "r", value.r) && TryGetFiniteFloat(*it, "g", value.g) &&
	TryGetFiniteFloat(*it, "b", value.b) && TryGetFiniteFloat(*it, "a", value.a) ?
	value : defaultValue;
}
