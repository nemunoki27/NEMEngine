#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <cstdint>

namespace Engine {

	struct RenderDrawContext;
	struct RenderItem;

	struct ShineHighlightData {

		Color4 color{};
		Vector2 direction{};
		float position = 0.0f;
		float width = 0.0f;
		float softness = 0.0f;
		float intensity = 0.0f;
		uint32_t rampIndex = UINT32_MAX;
		float padding = 0.0f;
	};
	static_assert(sizeof(ShineHighlightData) == 48);

	ShineHighlightData ResolveShineHighlight(const RenderDrawContext& context, const RenderItem& item);

} // Engine
