#include "JsonSerializer.h"

using namespace Engine;

//============================================================================*/
//	include
//============================================================================*/
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <cmath>
#include <filesystem>
#include <iterator>
#include <limits>

namespace {

	// JSON要素を有限なfloatとして読み込む
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

	bool CanonicalizeJson(nlohmann::json& value) {

		if (value.is_number_float()) {

			const double number = value.get<double>();
			if (!std::isfinite(number)) {
				return false;
			}
			if (number == 0.0) {
				value = 0.0;
			}
			return true;
		}
		if (value.is_array()) {
			for (auto& element : value) {
				if (!CanonicalizeJson(element)) {
					return false;
				}
			}
			return true;
		}
		if (value.is_object()) {
			for (auto it = value.begin(); it != value.end(); ++it) {
				if (!CanonicalizeJson(it.value())) {
					return false;
				}
			}
		}
		return true;
	}
}

//============================================================================*/
//	JsonAdapter classMethods
//============================================================================*/

void JsonAdapter::Save(const std::string& directoryFilePath, const nlohmann::json& data) {

	Save(Algorithm::PathFromUTF8(directoryFilePath), data);
}

void JsonAdapter::Save(const std::filesystem::path& directoryFilePath, const nlohmann::json& data) {

	// 親ディレクトリが無ければ作成する、ゲーム側Configなど初回保存でも失敗しないようにする
	const std::filesystem::path parentPath = directoryFilePath.parent_path();
	if (!parentPath.empty()) {
		std::error_code ec;
		std::filesystem::create_directories(parentPath, ec);
	}

	std::ofstream file(directoryFilePath);

	// 書き込めなかった場合
	if (!file.is_open()) {

		Assert::Call(false, "JSONファイルの保存に失敗しました: " + Algorithm::PathToUTF8(directoryFilePath));
		return;
	}

	file << data.dump(4); // インデント4で保存
}

bool JsonAdapter::SaveCanonical(const std::filesystem::path& directoryFilePath,
	const nlohmann::json& data, int32_t indent) {

	const std::string serialized = SerializeCanonical(data, indent);
	if (serialized.empty()) {
		return false;
	}

	std::ifstream currentFile(directoryFilePath, std::ios::binary);
	const std::string current((std::istreambuf_iterator<char>(currentFile)),
		std::istreambuf_iterator<char>());
	currentFile.close();
	if (current == serialized) {
		return true;
	}

	const std::filesystem::path parentPath = directoryFilePath.parent_path();
	std::error_code ec;
	if (!parentPath.empty()) {
		std::filesystem::create_directories(parentPath, ec);
		if (ec) {
			return false;
		}
	}

	std::filesystem::path tempPath = directoryFilePath;
	tempPath += L".tmp";
	{
		std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
		if (!file.is_open()) {
			return false;
		}
		file.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
		file.flush();
		if (!file.good()) {
			file.close();
			std::filesystem::remove(tempPath, ec);
			return false;
		}
	}

	const bool targetExists = std::filesystem::exists(directoryFilePath, ec);
	std::filesystem::path backupPath = directoryFilePath;
	backupPath += L".bak";
	if (targetExists) {

		std::filesystem::remove(backupPath, ec);
		ec.clear();
		std::filesystem::rename(directoryFilePath, backupPath, ec);
		if (ec) {
			std::filesystem::remove(tempPath, ec);
			return false;
		}
	}

	ec.clear();
	std::filesystem::rename(tempPath, directoryFilePath, ec);
	if (ec) {

		std::error_code rollbackError;
		if (targetExists) {
			std::filesystem::rename(backupPath, directoryFilePath, rollbackError);
		}
		std::filesystem::remove(tempPath, rollbackError);
		return false;
	}
	if (targetExists) {
		std::filesystem::remove(backupPath, ec);
	}
	return true;
}

std::string JsonAdapter::SerializeCanonical(const nlohmann::json& data, int32_t indent) {

	nlohmann::json canonical = data;
	if (!CanonicalizeJson(canonical)) {
		return {};
	}
	return canonical.dump(indent) + '\n';
}

nlohmann::json JsonAdapter::Load(const std::string& directoryFilePath, bool assertion) {

	return Load(Algorithm::PathFromUTF8(directoryFilePath), assertion);
}

nlohmann::json JsonAdapter::Load(const std::filesystem::path& directoryFilePath, bool assertion) {

	std::ifstream file(directoryFilePath);

	// 読み込めなかった場合
	if (!file.is_open()) {
		if (assertion) {

			Assert::Call(false, "JSONファイルの読み込みに失敗しました: " + Algorithm::PathToUTF8(directoryFilePath));
		}
		return nlohmann::json();
	}

	nlohmann::json data;
	try {
		file >> data;
	}
	catch (const nlohmann::json::parse_error& e) {
		if (assertion) {

			Assert::Call(false, "JSONファイルの解析に失敗しました: " +
				Algorithm::PathToUTF8(directoryFilePath) + "\n" + e.what());
		}
		return nlohmann::json();
	}

	return data;
}

bool JsonAdapter::Check(const std::string& directoryFilePath, bool assertion) {

	return Check(Algorithm::PathFromUTF8(directoryFilePath), assertion);
}

bool JsonAdapter::Check(const std::filesystem::path& directoryFilePath, bool assertion) {

	std::ifstream file(directoryFilePath);

	// 読み込めなかった場合
	if (!file.is_open()) {
		if (assertion) {

			Assert::Call(false, "JSONファイルの読み込みに失敗しました: " + Algorithm::PathToUTF8(directoryFilePath));
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

	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Vector2 value{};
	return TryGetFiniteFloat(*it, "x", value.x) && TryGetFiniteFloat(*it, "y", value.y) ?
		value : defaultValue;
}

void JsonAdapter::SetVector3(nlohmann::json& json, const std::string& key, const Vector3& value) {
	json[key] = { {"x", value.x}, {"y", value.y}, {"z", value.z} };
}

Vector3 JsonAdapter::GetVector3(const nlohmann::json& json, const std::string& key, const Vector3& defaultValue) {

	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Vector3 value{};
	return TryGetFiniteFloat(*it, "x", value.x) && TryGetFiniteFloat(*it, "y", value.y) &&
		TryGetFiniteFloat(*it, "z", value.z) ? value : defaultValue;
}

void JsonAdapter::SetVector4(nlohmann::json& json, const std::string& key, const Vector4& value) {
	json[key] = { {"x", value.x}, {"y", value.y}, {"z", value.z}, {"w", value.w} };
}

Vector4 JsonAdapter::GetVector4(const nlohmann::json& json, const std::string& key, const Vector4& defaultValue) {

	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Vector4 value{};
	return TryGetFiniteFloat(*it, "x", value.x) && TryGetFiniteFloat(*it, "y", value.y) &&
		TryGetFiniteFloat(*it, "z", value.z) && TryGetFiniteFloat(*it, "w", value.w) ?
		value : defaultValue;
}

void JsonAdapter::SetQuaternion(nlohmann::json& json, const std::string& key, const Quaternion& value) {
	// クォータニオンを{x, y, z, w}オブジェクトとして保存
	json[key] = { {"x", value.x}, {"y", value.y}, {"z", value.z}, {"w", value.w} };
}

Quaternion JsonAdapter::GetQuaternion(const nlohmann::json& json, const std::string& key, const Quaternion& defaultValue) {

	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Quaternion value{};
	return TryGetFiniteFloat(*it, "x", value.x) && TryGetFiniteFloat(*it, "y", value.y) &&
		TryGetFiniteFloat(*it, "z", value.z) && TryGetFiniteFloat(*it, "w", value.w) ?
		value : defaultValue;
}

void JsonAdapter::SetColor3(nlohmann::json& json, const std::string& key, const Color3& value) {
	// Color3を{r, g, b}オブジェクトとしてセット
	json[key] = { {"r", value.r}, {"g", value.g}, {"b", value.b} };
}

Color3 JsonAdapter::GetColor3(const nlohmann::json& json, const std::string& key, const Color3& defaultValue) {

	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Color3 value{};
	return TryGetFiniteFloat(*it, "r", value.r) && TryGetFiniteFloat(*it, "g", value.g) &&
		TryGetFiniteFloat(*it, "b", value.b) ? value : defaultValue;
}

void JsonAdapter::SetColor4(nlohmann::json& json, const std::string& key, const Color4& value) {
	// Color4を{r, g, b, a}オブジェクトとしてセット
	json[key] = { {"r", value.r}, {"g", value.g}, {"b", value.b}, {"a", value.a} };
}

Color4 JsonAdapter::GetColor4(const nlohmann::json& json, const std::string& key, const Color4& defaultValue) {

	const auto it = json.find(key);
	if (it == json.end() || !it->is_object()) {
		return defaultValue;
	}

	Color4 value{};
	return TryGetFiniteFloat(*it, "r", value.r) && TryGetFiniteFloat(*it, "g", value.g) &&
		TryGetFiniteFloat(*it, "b", value.b) && TryGetFiniteFloat(*it, "a", value.a) ?
		value : defaultValue;
}
