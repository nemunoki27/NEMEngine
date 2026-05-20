#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphID.h>

namespace Engine {

	//============================================================================
	//	GraphLink structure
	//	OutputPinからInputPinへの接続情報
	//============================================================================

	struct GraphLink {

		//--------- variables ----------------------------------------------------

		// Link ID
		GraphID id = 0;
		// 接続元Pin ID
		GraphID fromPinID = 0;
		// 接続先Pin ID
		GraphID toPinID = 0;
	};
} // Engine
