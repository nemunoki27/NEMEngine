#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphDocument.h>

// c++
#include <string>
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	GraphSerializer class
	//	汎用GraphDocumentのJSON変換を行うクラス
	//============================================================================
	class GraphSerializer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// GraphDocumentをJSONへ変換する
		static nlohmann::json ToJson(const GraphDocument& document);
		// JSONからGraphDocumentを復元する
		static bool FromJson(const nlohmann::json& data, GraphDocument& document, std::string* error = nullptr);

		// GraphDocumentをファイルへ保存する
		static bool Save(const std::string& path, const GraphDocument& document, std::string* error = nullptr);
		// ファイルからGraphDocumentを読み込む
		static bool Load(const std::string& path, GraphDocument& document, std::string* error = nullptr);
	};
} // Engine
