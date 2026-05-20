#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphDocument.h>

namespace Engine {

	//============================================================================
	//	NodeGraphInteraction class
	//	NodeGraphViewからGraphDocumentへ操作を反映するクラス
	//============================================================================
	class NodeGraphInteraction {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Pin同士を接続する
		static bool TryCreateLink(GraphDocument& document, GraphID fromPinID, GraphID toPinID);
		// Linkを削除する
		static bool TryDeleteLink(GraphDocument& document, GraphID linkId);
		// Nodeを削除する
		static bool TryDeleteNode(GraphDocument& document, GraphID nodeID);
	};
} // Engine
