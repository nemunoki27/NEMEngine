#pragma once

//============================================================================
//	include
//============================================================================
#include "CanvasComponent.h"

namespace Engine::CanvasComponentSerialization {

	// キーボードとGamepadの既定割当を取得する
	std::span<const CanvasInputBinding> GetDefaultInputBindings();
}
