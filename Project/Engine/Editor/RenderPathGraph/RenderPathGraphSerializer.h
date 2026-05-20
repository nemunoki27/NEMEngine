#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphDocument.h>

namespace Engine {

	//============================================================================
	//	RenderPathGraphSerializer class
	//	RenderPath Graphの保存と読み込みを行うクラス
	//============================================================================
	class RenderPathGraphSerializer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// ScenePathから既定のGraph保存Pathを作成する
		static std::string MakeDefaultGraphPath(const std::string& scenePath);
		// RenderPath Graphをファイルへ保存する
		static bool ExportGraph(const std::string& path, const GraphDocument& document, std::string* error = nullptr);
		// RenderPath Graphをファイルから読み込む
		static bool ImportGraph(const std::string& path, GraphDocument& document, std::string* error = nullptr);
	};
} // Engine
