#pragma once

//============================================================================
//	include
//============================================================================
#include <string>
#include <string_view>

namespace Engine::EditorManualNavigation {

	// 登録名からマニュアル生成側と同じページ名を求める
	std::string GetComponentPage(std::string_view component);
	// コンポーネントまたはスクリプトのマニュアルを開く
	bool OpenComponent(std::string_view component);
}
