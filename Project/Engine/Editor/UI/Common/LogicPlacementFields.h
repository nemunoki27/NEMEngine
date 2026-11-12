#pragma once

#include <Engine/Core/Foundation/Geometry/LogicPlacement.h>
#include <Engine/Editor/Utility/LogicPlacementSource.h>

namespace Engine::LogicPlacementFields {

	// ツールとSplineで同じ形状設定を描く
	bool DrawShape(LogicPlacementSettings& settings, bool spline = false);
	// 配置元を回転させる条件を描く
	bool DrawOrientation(LogicPlacementRequest& request);
}
