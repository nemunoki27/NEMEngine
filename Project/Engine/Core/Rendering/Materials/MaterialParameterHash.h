#pragma once

//============================================================================
//	include
//============================================================================
#include "MaterialParameter.h"

namespace Engine::MaterialParameterHash {

	// 値の型と内容からHashを作る
	uint64_t HashValue(const MaterialParameterValue& parameter);
} // Engine::MaterialParameterHash
