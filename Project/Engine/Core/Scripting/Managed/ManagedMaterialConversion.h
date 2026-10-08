#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>

namespace Engine::ManagedMaterialConversion {

	// ABI値をMaterial値へ変換する
	bool DecodeMaterialParameterValue(const ManagedMaterialParameterValue& source, MaterialParameterValue& outValue);
	// Material値をABI値へ変換する
	bool EncodeMaterialParameterValue(const MaterialParameterValue& source, ManagedMaterialParameterValue& outValue);
}
