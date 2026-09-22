#pragma once

//============================================================================
//	include
//============================================================================
#include "MaterialParameter.h"

namespace Engine::MaterialParameterLookup {

	// IDと意味と表示名の順に値を検索する
	const MaterialParameterValue* Find(const MaterialParameterSet& parameters, MaterialParameterID id,
		MaterialParameterSemantic semantic, std::string_view name, const MaterialParameterSet* defaults = nullptr);
}
