#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

namespace Engine::MaterialRenderPolicy {

	// 出力内容を保証できる標準のSurface Shaderか
	bool IsStandardSurface(const MaterialAsset& material);
	// Alpha判定なしで遮蔽を確定できるか
	bool IsOpaqueGeometry(const MaterialAsset* material, MaterialSurfaceMode mode);
	// 既定Materialの切替をcacheへ伝える
	uint64_t GetDefaultMaterialHash();
}
