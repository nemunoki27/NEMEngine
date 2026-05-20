#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphNode.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>

namespace Engine {

	//============================================================================
	//	RenderPathGraphAssetDragDrop class
	//	RenderPath Graph上で使用するAsset DragDrop処理
	//============================================================================
	class RenderPathGraphAssetDragDrop {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Material AssetのDragDropを受け取る
		static bool AcceptMaterial(GraphNode& node, const EditorToolContext& context);
	};
} // Engine
