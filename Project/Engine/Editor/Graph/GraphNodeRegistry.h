#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphDocument.h>
#include <Engine/Editor/Graph/GraphNodeDefinition.h>

// c++
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	GraphNodeRegistry class
	//	GraphNodeDefinitionを登録しNode生成を行うクラス
	//============================================================================
	class GraphNodeRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 登録済みのNode定義を全て削除する
		void Clear();
		// Node定義を登録する
		void Register(const GraphNodeDefinition& definition);

		// Node種別から定義を検索する
		const GraphNodeDefinition* Find(const std::string& type) const;
		// メニュー生成可能なNode定義を取得する
		std::vector<const GraphNodeDefinition*> GetCreatableDefinitions() const;
		// 定義を元にNodeを生成する
		GraphNode CreateNode(GraphDocument& document, const std::string& type, const ImVec2& position) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// typeをKeyにしたNode定義
		std::unordered_map<std::string, GraphNodeDefinition> definitions_;
	};
} // Engine
