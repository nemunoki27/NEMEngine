#pragma once

//============================================================================
//	include
//============================================================================
#include "AnimationClipAsset.h"

// c++
#include <span>

namespace Engine::AnimationChannelUtility {

	// チャネル名に対応する表示色を取得
	Color4 GetChannelColor(std::string_view name);
	// 全チャンネルに存在するキー数を取得する
	size_t GetSharedKeyCount(std::span<const CurveChannel> channels);
}
