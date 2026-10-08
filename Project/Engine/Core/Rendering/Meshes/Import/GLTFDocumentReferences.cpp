#include "GLTFDocumentReferences.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

// json
#include <json.hpp>

namespace {

	constexpr uint32_t kGLBMagic = 0x46546C67;
	constexpr uint32_t kJSONChunk = 0x4E4F534A;
	constexpr size_t kGLBHeaderSize = 12;
	constexpr size_t kChunkHeaderSize = 8;
	static_assert(std::endian::native == std::endian::little);

	// 未整列のbyte列からGLBの整数を読む
	uint32_t ReadUInt32(std::string_view bytes, size_t offset) {

		uint32_t value;
		std::memcpy(&value, bytes.data() + offset, sizeof(value));
		return value;
	}

	// JSONの長さ変更をGLBの整数へ反映する
	void WriteUInt32(std::string& bytes, size_t offset, uint32_t value) {

		std::memcpy(bytes.data() + offset, &value, sizeof(value));
	}

	// GLBの全Chunkを検査し先頭JSONの範囲を返す
	std::string_view GetBinaryJSON(std::string_view bytes) {

		if (bytes.size() < kGLBHeaderSize || ReadUInt32(bytes, 4) != 2 || ReadUInt32(bytes, 8) != bytes.size()) {
			throw std::runtime_error("GLBのHeaderが不正です");
		}
		std::string_view json;
		size_t offset = kGLBHeaderSize;
		while (offset < bytes.size()) {
			if (bytes.size() - offset < kChunkHeaderSize) {
				throw std::runtime_error("GLBのChunk Headerが不足しています");
			}
			const uint32_t length = ReadUInt32(bytes, offset);
			const uint32_t type = ReadUInt32(bytes, offset + 4);
			const bool first = offset == kGLBHeaderSize;
			offset += kChunkHeaderSize;
			if (length % 4 != 0 || bytes.size() - offset < length || (first ? type != kJSONChunk : type == kJSONChunk)) {
				throw std::runtime_error("GLBのChunkが不正です");
			}
			if (first) {
				json = bytes.substr(offset, length);
			}
			offset += length;
		}
		if (json.empty()) {
			throw std::runtime_error("GLBのJSONがありません");
		}
		return json;
	}

	// 任意のuriへ触れずBufferと画像の外部参照だけを更新する
	bool RewriteGroup(
		nlohmann::json& document, const char* name, const Engine::GLTFDocumentReferences::ReferenceRewrite& rewrite) {

		const auto group = document.find(name);
		if (group == document.end()) {
			return false;
		}
		// glTF1の名前付き項目もglTF2の配列と同じように扱う
		if (!group->is_array() && !group->is_object()) {
			throw std::runtime_error("glTFの参照一覧が不正です");
		}
		bool changed = false;
		for (auto& item : *group) {
			if (!item.is_object()) {
				throw std::runtime_error("glTFの参照項目が不正です");
			}
			const auto uri = item.find("uri");
			if (uri == item.end()) {
				continue;
			}
			if (!uri->is_string()) {
				throw std::runtime_error("glTFのuriが文字列ではありません");
			}
			std::string reference = uri->get<std::string>();
			if (reference.empty() || reference.starts_with("data:")) {
				continue;
			}
			if (!rewrite(reference)) {
				throw std::runtime_error("glTFの外部参照を更新できません");
			}
			if (reference != uri->get_ref<const std::string&>()) {
				*uri = std::move(reference);
				changed = true;
			}
		}
		return changed;
	}
}

bool Engine::GLTFDocumentReferences::IsDocumentPath(const std::filesystem::path& path) {

	const auto extension = Algorithm::ToLower(Algorithm::PathToUTF8(path.extension()));
	return extension == ".gltf" || extension == ".glb";
}

bool Engine::GLTFDocumentReferences::Rewrite(std::string& bytes, const ReferenceRewrite& rewrite, std::string& diagnostic) {

	diagnostic.clear();
	try {
		const bool binary = sizeof(uint32_t) <= bytes.size() && ReadUInt32(bytes, 0) == kGLBMagic;
		const std::string_view json = binary ? GetBinaryJSON(bytes) : std::string_view(bytes);
		auto document = nlohmann::json::parse(json);
		if (!document.is_object()) {
			throw std::runtime_error("glTFの文書が不正です");
		}
		// 途中失敗では入力byteを変更しない
		const bool buffersChanged = RewriteGroup(document, "buffers", rewrite);
		const bool imagesChanged = RewriteGroup(document, "images", rewrite);
		if (!buffersChanged && !imagesChanged) {
			return true;
		}
		std::string serialized = document.dump(4);
		if (!binary) {
			bytes = std::move(serialized);
			return true;
		}
		// JSONの後ろにあるBinaryと追加Chunkはそのまま保持する
		serialized.append((4 - serialized.size() % 4) % 4, ' ');
		const size_t jsonEnd = kGLBHeaderSize + kChunkHeaderSize + json.size();
		const size_t total = kGLBHeaderSize + kChunkHeaderSize + serialized.size() + bytes.size() - jsonEnd;
		if (total > (std::numeric_limits<uint32_t>::max)()) {
			throw std::runtime_error("更新したGLBが長さの上限を超えています");
		}
		std::string result = bytes.substr(0, kGLBHeaderSize + kChunkHeaderSize);
		result += serialized;
		result.append(bytes, jsonEnd, std::string::npos);
		WriteUInt32(result, 8, static_cast<uint32_t>(total));
		WriteUInt32(result, 12, static_cast<uint32_t>(serialized.size()));
		bytes = std::move(result);
		return true;
	} catch (const std::exception& error) {
		diagnostic = error.what();
		return false;
	}
}
