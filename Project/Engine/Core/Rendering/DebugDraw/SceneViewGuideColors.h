#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Color.h>

namespace Engine {

	// SceneViewの描画パスが参照する線形色
	struct SceneViewGuideColors {

		Color4 grid2D, gridMinor, gridMajor, gridCoarse, gridSnap, gridX, gridZ;
		Color4 cameraBounds, cameraIcon, disabledIconTint;

		static SceneViewGuideColors& Get() {
			static SceneViewGuideColors colors{};
			return colors;
		}
	};
}
