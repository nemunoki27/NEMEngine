#pragma once

//============================================================================
//	include
//============================================================================
#include "TimelineEditorFields.h"

namespace Engine::TimelineClipFields {

	// 型付きの共通フィールドをクリップ設定へ接続する
	ValueEditResult Bool(const char* label, nlohmann::json& data, const char* key, bool fallback = false);
	ValueEditResult Number(const char* label, nlohmann::json& data, const char* key, float fallback = 0.0f,
		float minimum = -1000000.0f, float maximum = 1000000.0f);
	ValueEditResult Integer(const char* label, nlohmann::json& data, const char* key, int fallback = 0);
	ValueEditResult Text(const char* label, nlohmann::json& data, const char* key);
	ValueEditResult Vector(const char* label, nlohmann::json& data, const char* key, Vector3 fallback = {});
	ValueEditResult Asset(const char* label, nlohmann::json& data, const char* key,
		AssetDatabase* database, std::initializer_list<AssetType> types);
	ValueEditResult Binding(const char* label, nlohmann::json& data, const char* key, const TimelineAsset& asset);
}
