#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphAsset.h"

namespace Engine::ShaderGraphResourceInputs {

	// Textureの直接値と未接続入力を共通のParameterへ正規化する
	void Normalize(ShaderGraphAsset& graph);
}
