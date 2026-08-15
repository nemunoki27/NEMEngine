#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>

namespace Engine {

	// DXCリフレクション出力を共通形式へ変換する
	bool ParseDxShaderReflection(IDxcUtils* dxcUtils,
		const DxcBuffer& reflectionBuffer, ShaderStage stage,
		ShaderReflectionInfo& outReflection);
} // Engine
