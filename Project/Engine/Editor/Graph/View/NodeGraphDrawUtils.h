#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphPin.h>
#include <Engine/Editor/Graph/View/NodeGraphStyle.h>

namespace Engine::NodeGraphDrawUtils {

	//============================================================================
	//	NodeGraphDrawUtils functions
	//	NodeGraph描画用の補助関数
	//============================================================================

	// ピン種別に応じた小さな記号を描画する
	void DrawPinIcon(const GraphPin& pin, const NodeGraphStyle& style);
}
