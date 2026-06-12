#include "ManagedScriptExceptionStore.h"

//============================================================================
//	include
//============================================================================
#include <json.hpp>

// c++
#include <ctime>

namespace {

	// HH:MM:SSのタイムスタンプManagedBuildDiagnosticStoreと同じ書式に合わせる
	std::string NowTimeString() {
		const std::time_t now = std::time(nullptr);
		std::tm local{};
#if defined(_WIN32)
		localtime_s(&local, &now);
#else
		localtime_r(&now, &local);
#endif
		char buffer[16]{};
		std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
		return std::string(buffer);
	}

	// JSONから文字列を安全に取り出す型不一致や欠落は空文字にし上限で切り詰める
	std::string ReadString(const nlohmann::json& node, const char* key, size_t maxLength) {

		auto it = node.find(key);
		if (it == node.end() || !it->is_string()) {
			return std::string();
		}
		std::string value = it->get<std::string>();
		if (value.size() > maxLength) {
			value.resize(maxLength);
		}
		return value;
	}

	// JSONから整数を取り出す欠落や型不一致はfallbackを返す
	int64_t ReadInt(const nlohmann::json& node, const char* key, int64_t fallback) {

		auto it = node.find(key);
		if (it == node.end() || !it->is_number_integer() && !it->is_number_unsigned()) {
			return fallback;
		}
		return it->get<int64_t>();
	}
}

void Engine::ManagedScriptExceptionStore::ReportJson(const char* jsonUtf8) {

	if (!jsonUtf8 || jsonUtf8[0] == '\0') {
		return;
	}

	// 報告点で1度だけparseし例外発生時のみ通る経路なのでhot pathには乗らない、壊れたDTOでengineを巻き込まないようparse失敗は握り潰す
	nlohmann::json root = nlohmann::json::parse(jsonUtf8, nullptr, false);
	if (root.is_discarded() || !root.is_object()) {
		return;
	}

	ManagedScriptException entry{};
	entry.id = nextId_++;
	entry.timestamp = NowTimeString();
	entry.callback = ReadString(root, "callback", kMaxStringLength);
	entry.scriptSlotId = static_cast<uint64_t>(ReadInt(root, "slotId", 0));
	entry.scriptTypeId = ReadString(root, "scriptTypeId", kMaxStringLength);
	entry.typeName = ReadString(root, "typeName", kMaxStringLength);
	entry.exceptionType = ReadString(root, "exceptionType", kMaxStringLength);
	entry.message = ReadString(root, "message", kMaxMessageLength);
	entry.entityIndex = static_cast<uint32_t>(ReadInt(root, "entityIndex", 0));
	entry.entityGeneration = static_cast<uint32_t>(ReadInt(root, "entityGeneration", 0));
	entry.entityName = ReadString(root, "entityName", kMaxStringLength);

	// stack frameは上限まで深い再帰例外で履歴が肥大しないようにする
	auto framesIt = root.find("frames");
	if (framesIt != root.end() && framesIt->is_array()) {
		for (const nlohmann::json& frameNode : *framesIt) {

			if (entry.frames.size() >= kMaxFrames) {
				break;
			}
			if (!frameNode.is_object()) {
				continue;
			}
			ManagedScriptExceptionFrame frame{};
			frame.method = ReadString(frameNode, "method", kMaxStringLength);
			frame.file = ReadString(frameNode, "file", kMaxStringLength);
			frame.line = static_cast<int32_t>(ReadInt(frameNode, "line", 0));
			frame.column = static_cast<int32_t>(ReadInt(frameNode, "column", 0));
			entry.frames.push_back(std::move(frame));
		}
	}

	entries_.push_back(std::move(entry));
	EnforceBounds();
	++version_;
}

void Engine::ManagedScriptExceptionStore::Clear() {

	if (entries_.empty()) {
		return;
	}
	entries_.clear();
	++version_;
}

void Engine::ManagedScriptExceptionStore::EnforceBounds() {

	// 古い側から間引いて最新の例外を優先して残す
	while (entries_.size() > kMaxEntries) {
		entries_.pop_front();
	}
}

Engine::ManagedScriptExceptionStore& Engine::ManagedScriptExceptionStore::GetInstance() {

	static ManagedScriptExceptionStore store;
	return store;
}
