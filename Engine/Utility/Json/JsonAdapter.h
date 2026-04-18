#pragma once

//============================================================================*/
//	include
//============================================================================*/
#include <Engine/MathLib/Vector2.h>
#include <Engine/MathLib/Vector3.h>
#include <Engine/MathLib/Vector4.h>
#include <Engine/MathLib/Quaternion.h>

// c++
#include <string>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <vector>
#include <array>
#include <cassert>

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
		// 読み込み
		static nlohmann::json Load(const std::string& directoryFilePath, bool assertion = false);

		// 読みこめるかチェック
		static bool Check(const std::string& directoryFilePath, bool assertion = false);
	};
}; // Engine
