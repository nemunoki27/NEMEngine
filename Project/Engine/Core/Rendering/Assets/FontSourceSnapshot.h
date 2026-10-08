#pragma once

//============================================================================
//	include
//============================================================================
#include "MSDFFontAsset.h"
#include <Engine/Core/Rendering/Textures/TextureFileRequest.h>

namespace Engine {

	// 配置情報と対応するAtlas画像を保持する読込世代
	struct FontSourceSnapshot {

		MSDFFontAsset font;
		TextureFileRequestDesc atlas;
	};
} // namespace Engine
