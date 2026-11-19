#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphAsset.h"

namespace Engine::ShaderGraphPreviewUniforms {

	// プレビュー用コピーの編集値を転送可能な定数へ分離する
	void Lift(ShaderGraphAsset& graph);
}
