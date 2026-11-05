#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderFeatureProfile.h"

namespace Engine {

	// 旧FXAAを固定アンチエイリアスへ移し、入力参照を接続し直す
	void MigrateRenderFeatureAntialiasing(RenderFeatureProfileAsset& profile);
}
