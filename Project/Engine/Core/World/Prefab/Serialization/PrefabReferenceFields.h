#pragma once

//============================================================================
//	include
//============================================================================
#include "PrefabReferenceRemapper.h"

namespace Engine::PrefabReferenceFields {

	// JSON値からUUIDを読む
	UUID ReadUUIDValue(const nlohmann::json& value);
	// 指定キーのUUID値を読む
	UUID ReadUUIDKey(const nlohmann::json& object, const char* key);
	// 指定キーのUUID値を張り替える
	void RemapUUIDKey(nlohmann::json& object, const char* key, const PrefabReferenceRemapper::LocalFileIDMap& localFileIDMap);
	// 保存Entity参照の形式を判定する
	bool IsEntityRefObject(const nlohmann::json& value);
	// 組込みComponentのローカル参照を張り替える
	void RemapNativeComponentFields(const std::string& componentType, nlohmann::json& component,
		const PrefabReferenceRemapper::LocalFileIDMap& localFileIDMap);
	// ローカルIDを指す差分パスを判定する
	bool IsLocalFileIDLeafPath(const std::string& path);
	// 差分値のローカルIDを張り替える
	void RemapLeafValue(nlohmann::json& value, const PrefabReferenceRemapper::LocalFileIDMap& localFileIDMap);
}
