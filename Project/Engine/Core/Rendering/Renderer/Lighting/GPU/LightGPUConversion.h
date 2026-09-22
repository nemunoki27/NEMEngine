#pragma once

//============================================================================
//	include
//============================================================================
#include "LightGPUTypes.h"
#include <Engine/Core/Rendering/Renderer/Lighting/FrameLightBatch.h>

namespace Engine::LightGPUConversion {

	DirectionalLightGPU ToGPU(const DirectionalLightItem& item);
	PointLightGPU ToGPU(const PointLightItem& item);
	SpotLightGPU ToGPU(const SpotLightItem& item);
	RectLightGPU ToGPU(const RectLightItem& item);
}
