#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Vector4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>
#include <Engine/Core/Foundation/Math/Color.h>

#include <string>
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	JsonMath class
	//	JSONの数学型変換
	//============================================================================
	class JsonMath {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		JsonMath() = delete;
		~JsonMath() = delete;

		//--------- accessor -----------------------------------------------------

		// 2次元ベクトルを保存・取得する
		static void SetVector2(nlohmann::json& json, const std::string& key, const Vector2& value);
		static Vector2 GetVector2(const nlohmann::json& json, const std::string& key, const Vector2& defaultValue);
		// 3次元ベクトルを保存・取得する
		static void SetVector3(nlohmann::json& json, const std::string& key, const Vector3& value);
		static Vector3 GetVector3(const nlohmann::json& json, const std::string& key, const Vector3& defaultValue);
		// 4成分ベクトルを保存・取得する
		static void SetVector4(nlohmann::json& json, const std::string& key, const Vector4& value);
		static Vector4 GetVector4(const nlohmann::json& json, const std::string& key, const Vector4& defaultValue);
		// 回転を保存・取得する
		static void SetQuaternion(nlohmann::json& json, const std::string& key, const Quaternion& value);
		static Quaternion GetQuaternion(const nlohmann::json& json, const std::string& key, const Quaternion& defaultValue);
		// RGB色を保存・取得する
		static void SetColor3(nlohmann::json& json, const std::string& key, const Color3& value);
		static Color3 GetColor3(const nlohmann::json& json, const std::string& key, const Color3& defaultValue);
		// RGBA色を保存・取得する
		static void SetColor4(nlohmann::json& json, const std::string& key, const Color4& value);
		static Color4 GetColor4(const nlohmann::json& json, const std::string& key, const Color4& defaultValue);
	};
}
