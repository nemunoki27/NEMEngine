#include "ManagedScriptExceptionParser.h"

//============================================================================
//	include
//============================================================================
// c++
#include <limits>
#include <utility>

#include <json.hpp>

namespace {

	// UTF8の文字境界で表示文字列を切り詰める
	std::string ReadString(const nlohmann::json& node, const char* key, size_t maxLength) {

		const auto it = node.find(key);
		if (it == node.end() || !it->is_string()) {
			return {};
		}
		std::string value = it->get<std::string>();
		if (value.size() > maxLength) {
			size_t length = maxLength;
			while (length > 0 && (static_cast<uint8_t>(value[length]) & 0xC0) == 0x80) {
				--length;
			}
			value.resize(length);
		}
		return value;
	}

	// 負数と範囲外を除外して番号を読み込む
	template <typename T>
	T ReadNumber(const nlohmann::json& node, const char* key, T fallback) {

		const auto it = node.find(key);
		if (it == node.end() || !it->is_number_integer()) {
			return fallback;
		}
		if (!it->is_number_unsigned() && it->get<int64_t>() < 0) {
			return fallback;
		}
		const uint64_t value = it->get<uint64_t>();
		return value <= static_cast<uint64_t>(std::numeric_limits<T>::max()) ? static_cast<T>(value) : fallback;
	}
}

bool Engine::ParseManagedScriptException(const char* jsonUTF8, ManagedScriptException& entry) {

	if (!jsonUTF8) {
		return false;
	}
	// 診断入力の大きさを制限してから解析する
	constexpr size_t kMaxJSONBytes = 256 * 1024;
	size_t length = 0;
	while (length <= kMaxJSONBytes && jsonUTF8[length] != '\0') {
		++length;
	}
	if (length == 0 || length > kMaxJSONBytes) {
		return false;
	}
	const auto root = nlohmann::json::parse(jsonUTF8, jsonUTF8 + length, nullptr, false);
	if (root.is_discarded() || !root.is_object()) {
		return false;
	}
	using Store = ManagedScriptExceptionStore;
	ManagedScriptException candidate{};
	candidate.callback = ReadString(root, "callback", Store::kMaxStringLength);
	candidate.scriptSlotID = ReadNumber<uint64_t>(root, "slotId", 0);
	candidate.scriptTypeID = ReadString(root, "scriptTypeId", Store::kMaxStringLength);
	candidate.typeName = ReadString(root, "typeName", Store::kMaxStringLength);
	candidate.exceptionType = ReadString(root, "exceptionType", Store::kMaxStringLength);
	candidate.message = ReadString(root, "message", Store::kMaxMessageLength);
	candidate.worldIndex = ReadNumber<uint32_t>(root, "worldIndex", UINT32_MAX);
	candidate.worldGeneration = ReadNumber<uint32_t>(root, "worldGeneration", 0);
	candidate.entityIndex = ReadNumber<uint32_t>(root, "entityIndex", UINT32_MAX);
	candidate.entityGeneration = ReadNumber<uint32_t>(root, "entityGeneration", 0);
	candidate.entityName = ReadString(root, "entityName", Store::kMaxStringLength);

	// 深いstackは表示上限まで保持する
	const auto frames = root.find("frames");
	if (frames != root.end() && frames->is_array()) {
		for (const auto& node : *frames) {
			if (candidate.frames.size() >= Store::kMaxFrames) {
				break;
			}
			if (!node.is_object()) {
				continue;
			}
			ManagedScriptExceptionFrame frame{};
			frame.method = ReadString(node, "method", Store::kMaxStringLength);
			frame.file = ReadString(node, "file", Store::kMaxStringLength);
			frame.line = ReadNumber<int32_t>(node, "line", 0);
			frame.column = ReadNumber<int32_t>(node, "column", 0);
			candidate.frames.push_back(std::move(frame));
		}
	}
	entry = std::move(candidate);
	return true;
}
