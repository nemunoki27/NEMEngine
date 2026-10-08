#pragma once

//============================================================================
//	include
//============================================================================
#include "MaterialParameter.h"

namespace Engine {

	struct ShaderConstantBufferVariable;
}

namespace Engine::MaterialParameterDefaults {

	// Metadataと名前から色パラメータを判定する
	bool IsColor(const ShaderConstantBufferVariable& variable);
	// Shader変数の型に対応した既定値を作る
	MaterialParameterValue BuildValue(const ShaderConstantBufferVariable& variable);
}
