#pragma once

//============================================================================
//	include
//============================================================================
#include "AnimationClipAsset.h"

namespace Engine::AnimationChannelUtility {

	// チャネル名に対応する表示色を取得
	Color4 GetChannelColor(std::string_view name);
}
