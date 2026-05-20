#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphDocument.h>

namespace Engine {

	//============================================================================
	//	RenderPathGraphDocument structure
	//	RenderPath用のGraphDocumentを保持する構造体
	//============================================================================
	struct RenderPathGraphDocument {

		//--------- variables ----------------------------------------------------

		// 汎用Node Graph本体
		GraphDocument graph;
	};
} // Engine
