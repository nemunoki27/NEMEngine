#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <string_view>

namespace Engine::ScriptExecutionOrderSettings {

	// Script Type GUIDへ設定された上書き値を返し、未設定なら既定値を返す
	int32_t Resolve(const std::string_view& scriptTypeID, int32_t defaultOrder);

	// Script Type GUIDが上書き値を持つか
	bool HasOverride(const std::string_view& scriptTypeID);

	// Script Type GUIDへ実行順を設定する
	bool SetOverride(const std::string_view& scriptTypeID, int32_t order);

	// Script Type GUIDの上書き値を削除する
	bool RemoveOverride(const std::string_view& scriptTypeID);

	// 現在の上書き値をProjectSettingsへ保存する
	bool Save();

	// ProjectSettingsから上書き値を読み直す
	void Reload();

} // Engine::ScriptExecutionOrderSettings
