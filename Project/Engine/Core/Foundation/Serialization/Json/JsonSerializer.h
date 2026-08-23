#pragma once

//============================================================================*/
//	include
//============================================================================*/
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Vector4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <string>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <vector>
#include <array>

namespace Engine {

	//============================================================================*/
	//	JsonAdapter class
	//	jsonの保存、読み込み、変換を行うアダプター
	//============================================================================*/
	class JsonAdapter {
	public:
		//========================================================================*/
		//	public Methods
		//========================================================================*/

		JsonAdapter() = default;
		~JsonAdapter() = default;

		// 保存
		static void Save(const std::string& directoryFilePath, const nlohmann::json& data);
		static void Save(const std::filesystem::path& directoryFilePath, const nlohmann::json& data);
		// キー順と数値表現を正規化し、同一内容なら書き換えず安全に保存
		static bool SaveCanonical(const std::filesystem::path& directoryFilePath,
			const nlohmann::json& data, int32_t indent = 4);
		// Canonical JSONをUTF-8文字列へ変換
		static std::string SerializeCanonical(const nlohmann::json& data, int32_t indent = 4);
		// 読み込み
		static nlohmann::json Load(const std::string& directoryFilePath, bool assertion = false);
		static nlohmann::json Load(const std::filesystem::path& directoryFilePath, bool assertion = false);

		// 読みこめるかチェック
		static bool Check(const std::string& directoryFilePath, bool assertion = false);
		static bool Check(const std::filesystem::path& directoryFilePath, bool assertion = false);

		//--------- math accessor ------------------------------------------------

		// Vector/Quaternion/Colorの読み書きでGetはキーやJSON形式が不正ならdefaultValueを返す
		static void SetVector2(nlohmann::json& json, const std::string& key, const Vector2& value);
		static Vector2 GetVector2(const nlohmann::json& json, const std::string& key, const Vector2& defaultValue = {});
		static void SetVector3(nlohmann::json& json, const std::string& key, const Vector3& value);
		static Vector3 GetVector3(const nlohmann::json& json, const std::string& key, const Vector3& defaultValue = {});
		static void SetVector4(nlohmann::json& json, const std::string& key, const Vector4& value);
		static Vector4 GetVector4(const nlohmann::json& json, const std::string& key, const Vector4& defaultValue = {});
		static void SetQuaternion(nlohmann::json& json, const std::string& key, const Quaternion& value);
		static Quaternion GetQuaternion(const nlohmann::json& json, const std::string& key, const Quaternion& defaultValue = {});
		static void SetColor3(nlohmann::json& json, const std::string& key, const Color3& value);
		static Color3 GetColor3(const nlohmann::json& json, const std::string& key, const Color3& defaultValue = {});
		static void SetColor4(nlohmann::json& json, const std::string& key, const Color4& value);
		static Color4 GetColor4(const nlohmann::json& json, const std::string& key, const Color4& defaultValue = {});
	};
}; // Engine
