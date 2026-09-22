#pragma once

//============================================================================
//	include
//============================================================================
#include "EditorLayoutTypes.h"

namespace Engine {

	// 保存元を含むレイアウト
	struct EditorStoredLayout {

		EditorLayoutSnapshot layout;
		bool imported = false;
	};
}
