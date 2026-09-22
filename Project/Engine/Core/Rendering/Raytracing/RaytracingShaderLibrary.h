#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/ShaderAsset.h>

namespace Engine {

	class DxShaderCompiler;
	namespace RaytracingShaderLibrary {
		// CookまたはSourceからDXRライブラリを取得する
		CompiledShader Load(DxShaderCompiler* compiler, const ShaderStageEntry& stage);
	}
}
