#pragma once

#include <Engine/Core/Foundation/Geometry/LogicPlacement.h>
#include <Engine/Core/World/Components/Transform/SplineComponent.h>

namespace Engine {

	// 形状の順序に沿って接線付きの制御点を作る
	bool ApplyLogicPlacementSpline(const LogicPlacementSettings& settings, const LogicPlacementResult& layout, SplineData& data);
}
