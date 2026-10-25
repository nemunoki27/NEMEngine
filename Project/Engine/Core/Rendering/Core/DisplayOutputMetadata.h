#pragma once

//============================================================================
//	include
//============================================================================
#include <dxgi1_5.h>

namespace Engine {

	// 指定した最大輝度からHDR10の表示メタデータを構築する
	DXGI_HDR_METADATA_HDR10 BuildDisplayOutputMetadata(float maxLuminanceNits);
}
