#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>
#include <Engine/Core/Scripting/Managed/ManagedBridgeExports.h>

#include <unordered_map>

namespace Engine {

	//============================================================================
	//	ManagedSchemaCache class
	//	Assemblyに対応するスキーマを保持する
	//============================================================================
	class ManagedSchemaCache {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// スキーマを取得し必要なら解析する
		const ManagedScriptSchema& Get(const std::string& scriptTypeID, bool initialized, const ManagedBridgeExports& bridge);
		// Assembly切替時に解析結果を破棄する
		void Clear();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::unordered_map<std::string, ManagedScriptSchema> schemaCache_;
	};
}
