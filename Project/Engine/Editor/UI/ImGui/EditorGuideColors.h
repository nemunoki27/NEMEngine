#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Editor/UI/ImGui/EditorPalette.h>

namespace Engine::EditorGuideColors {

	// 色見本のsRGBからシーン描画用の線形色に変換する
	inline Color4 Linear(const char* key) {

		const ImVec4 color = EditorPalette::Get(key);
		return {Color4::SRGBToLinear(color.x), Color4::SRGBToLinear(color.y), Color4::SRGBToLinear(color.z), color.w};
	}
}
