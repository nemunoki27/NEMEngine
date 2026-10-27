#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>

namespace Engine::ManagedMaterialConversion {

	// 表示名のHashを保存済みの安定IDへ対応付ける
	MaterialParameterID ResolveParameterID(const MaterialParameterSet& parameters, MaterialParameterID id);
	// 安定IDと表示名の両方から値を取得する
	const MaterialParameterValue* FindParameter(const MaterialParameterSet& parameters, MaterialParameterID id);
	// 色とVector4以外の型変更を拒否する
	bool IsParameterTypeCompatible(const MaterialParameterValue* current, const MaterialParameterValue& replacement);

	// ABI値をMaterial値へ変換する
	bool DecodeMaterialParameterValue(const ManagedMaterialParameterValue& source, MaterialParameterValue& outValue);
	// Material値をABI値へ変換する
	bool EncodeMaterialParameterValue(const MaterialParameterValue& source, ManagedMaterialParameterValue& outValue);
}
