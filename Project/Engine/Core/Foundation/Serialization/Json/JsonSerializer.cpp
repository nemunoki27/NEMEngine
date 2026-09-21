#include "JsonSerializer.h"

//============================================================================
//	include
//============================================================================
#include "JsonFile.h"
#include "JsonCanonical.h"
#include "JsonMath.h"

using namespace Engine;

void JsonAdapter::Save(const std::string& directoryFilePath, const nlohmann::json& data) {

	JsonFile::Save(directoryFilePath, data);
}

void JsonAdapter::Save(const std::filesystem::path& directoryFilePath, const nlohmann::json& data) {

	JsonFile::Save(directoryFilePath, data);
}

bool JsonAdapter::SaveCanonical(const std::filesystem::path& directoryFilePath,
	const nlohmann::json& data, int32_t indent) {

	return JsonFile::SaveCanonical(directoryFilePath, data, indent);
}

nlohmann::json JsonAdapter::Load(const std::string& directoryFilePath, bool assertion) {

	return JsonFile::Load(directoryFilePath, assertion);
}

nlohmann::json JsonAdapter::Load(const std::filesystem::path& directoryFilePath, bool assertion) {

	return JsonFile::Load(directoryFilePath, assertion);
}

bool JsonAdapter::Check(const std::string& directoryFilePath, bool assertion) {

	return JsonFile::Check(directoryFilePath, assertion);
}

bool JsonAdapter::Check(const std::filesystem::path& directoryFilePath, bool assertion) {

	return JsonFile::Check(directoryFilePath, assertion);
}

std::string JsonAdapter::SerializeCanonical(const nlohmann::json& data, int32_t indent) {

	return JsonCanonical::SerializeCanonical(data, indent);
}

void JsonAdapter::SetVector2(nlohmann::json& json, const std::string& key, const Vector2& value) {

	JsonMath::SetVector2(json, key, value);
}

Vector2 JsonAdapter::GetVector2(const nlohmann::json& json, const std::string& key, const Vector2& defaultValue) {

	return JsonMath::GetVector2(json, key, defaultValue);
}

void JsonAdapter::SetVector3(nlohmann::json& json, const std::string& key, const Vector3& value) {

	JsonMath::SetVector3(json, key, value);
}

Vector3 JsonAdapter::GetVector3(const nlohmann::json& json, const std::string& key, const Vector3& defaultValue) {

	return JsonMath::GetVector3(json, key, defaultValue);
}

void JsonAdapter::SetVector4(nlohmann::json& json, const std::string& key, const Vector4& value) {

	JsonMath::SetVector4(json, key, value);
}

Vector4 JsonAdapter::GetVector4(const nlohmann::json& json, const std::string& key, const Vector4& defaultValue) {

	return JsonMath::GetVector4(json, key, defaultValue);
}

void JsonAdapter::SetQuaternion(nlohmann::json& json, const std::string& key, const Quaternion& value) {

	JsonMath::SetQuaternion(json, key, value);
}

Quaternion JsonAdapter::GetQuaternion(const nlohmann::json& json, const std::string& key, const Quaternion& defaultValue) {

	return JsonMath::GetQuaternion(json, key, defaultValue);
}

void JsonAdapter::SetColor3(nlohmann::json& json, const std::string& key, const Color3& value) {

	JsonMath::SetColor3(json, key, value);
}

Color3 JsonAdapter::GetColor3(const nlohmann::json& json, const std::string& key, const Color3& defaultValue) {

	return JsonMath::GetColor3(json, key, defaultValue);
}

void JsonAdapter::SetColor4(nlohmann::json& json, const std::string& key, const Color4& value) {

	JsonMath::SetColor4(json, key, value);
}

Color4 JsonAdapter::GetColor4(const nlohmann::json& json, const std::string& key, const Color4& defaultValue) {

	return JsonMath::GetColor4(json, key, defaultValue);
}
