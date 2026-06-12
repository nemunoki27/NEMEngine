#include "JsonSerializer.h"

using namespace Engine;

//============================================================================*/
//	include
//============================================================================*/
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

//============================================================================*/
//	JsonAdapter classMethods
//============================================================================*/

void JsonAdapter::Save(const std::string& directoryFilePath, const nlohmann::json& data) {

	const std::string fullPath = directoryFilePath;
	std::ofstream file(fullPath);

	// 書き込めなかった場合
	if (!file.is_open()) {

		Assert::Call(false, "Failed to save nlohmann::json file: " + fullPath);
		return;
	}

	file << data.dump(4); // インデント4で保存
}

nlohmann::json JsonAdapter::Load(const std::string& directoryFilePath, bool assertion) {

	const std::string fullPath = directoryFilePath;
	std::ifstream file(fullPath);

	// 読み込めなかった場合
	if (!file.is_open()) {
		if (assertion) {

			Assert::Call(false, "Failed to load nlohmann::json file: " + fullPath);
		}
		return nlohmann::json();
	}

	nlohmann::json data;
	try {
		file >> data;
	}
	catch (const nlohmann::json::parse_error& e) {
		if (assertion) {

			Assert::Call(false, "Failed to parse nlohmann::json file: " + fullPath + "\n" + e.what());
		}
		return nlohmann::json();
	}

	return data;
}

bool JsonAdapter::Check(const std::string& directoryFilePath, bool assertion) {

	const std::string fullPath = directoryFilePath;
	std::ifstream file(fullPath);

	// 読み込めなかった場合
	if (!file.is_open()) {
		if (assertion) {

			Assert::Call(false, "Failed to load nlohmann::json file: " + fullPath);
		}
		return false;
	}
	return true;
}

//============================================================================
//	Math Helpers
//============================================================================

void JsonAdapter::SetVector2(nlohmann::json& json, const std::string& key, const Vector2& value) {
	// Vector2を{x, y}オブジェクトとしてセット
	json[key] = { {"x", value.x}, {"y", value.y} };
}

Vector2 JsonAdapter::GetVector2(const nlohmann::json& json, const std::string& key, const Vector2& defaultValue) {
	// キーが存在しかつオブジェクト形式であれば値を読み取り、欠落時はデフォルト値を返す
	if (json.contains(key) && json[key].is_object()) {
		const auto& v = json[key];
		return Vector2(v.value("x", defaultValue.x), v.value("y", defaultValue.y));
	}
	return defaultValue;
}

void JsonAdapter::SetVector3(nlohmann::json& json, const std::string& key, const Vector3& value) {
	json[key] = { {"x", value.x}, {"y", value.y}, {"z", value.z} };
}

Vector3 JsonAdapter::GetVector3(const nlohmann::json& json, const std::string& key, const Vector3& defaultValue) {
	if (json.contains(key) && json[key].is_object()) {
		const auto& v = json[key];
		return Vector3(v.value("x", defaultValue.x), v.value("y", defaultValue.y), v.value("z", defaultValue.z));
	}
	return defaultValue;
}

void JsonAdapter::SetVector4(nlohmann::json& json, const std::string& key, const Vector4& value) {
	json[key] = { {"x", value.x}, {"y", value.y}, {"z", value.z}, {"w", value.w} };
}

Vector4 JsonAdapter::GetVector4(const nlohmann::json& json, const std::string& key, const Vector4& defaultValue) {
	if (json.contains(key) && json[key].is_object()) {
		const auto& v = json[key];
		return Vector4(v.value("x", defaultValue.x), v.value("y", defaultValue.y), v.value("z", defaultValue.z), v.value("w", defaultValue.w));
	}
	return defaultValue;
}

void JsonAdapter::SetQuaternion(nlohmann::json& json, const std::string& key, const Quaternion& value) {
	// クォータニオンを{x, y, z, w}オブジェクトとして保存
	json[key] = { {"x", value.x}, {"y", value.y}, {"z", value.z}, {"w", value.w} };
}

Quaternion JsonAdapter::GetQuaternion(const nlohmann::json& json, const std::string& key, const Quaternion& defaultValue) {
	if (json.contains(key) && json[key].is_object()) {
		const auto& v = json[key];
		return Quaternion(v.value("x", defaultValue.x), v.value("y", defaultValue.y), v.value("z", defaultValue.z), v.value("w", defaultValue.w));
	}
	return defaultValue;
}

void JsonAdapter::SetColor3(nlohmann::json& json, const std::string& key, const Color3& value) {
	// Color3を{r, g, b}オブジェクトとしてセット
	json[key] = { {"r", value.r}, {"g", value.g}, {"b", value.b} };
}

Color3 JsonAdapter::GetColor3(const nlohmann::json& json, const std::string& key, const Color3& defaultValue) {
	if (json.contains(key) && json[key].is_object()) {
		const auto& v = json[key];
		return Color3(v.value("r", defaultValue.r), v.value("g", defaultValue.g), v.value("b", defaultValue.b));
	}
	return defaultValue;
}

void JsonAdapter::SetColor4(nlohmann::json& json, const std::string& key, const Color4& value) {
	// Color4を{r, g, b, a}オブジェクトとしてセット
	json[key] = { {"r", value.r}, {"g", value.g}, {"b", value.b}, {"a", value.a} };
}

Color4 JsonAdapter::GetColor4(const nlohmann::json& json, const std::string& key, const Color4& defaultValue) {
	if (json.contains(key) && json[key].is_object()) {
		const auto& v = json[key];
		return Color4(v.value("r", defaultValue.r), v.value("g", defaultValue.g), v.value("b", defaultValue.b), v.value("a", defaultValue.a));
	}
	return defaultValue;
}
