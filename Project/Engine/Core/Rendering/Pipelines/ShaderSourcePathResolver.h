#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <unordered_map>

//============================================================================
//	ShaderSourcePathResolver
//	shader.jsonやインラインdescで指定されたシェーダーソース参照を実体パスへ解決する
//	参照は.metaのguidでもShadersルート相対パスでも受け付ける
//	PipelineStateとRaytracingPipelineStateで同じ解決を使うため共通化している
//============================================================================
namespace Engine::ShaderSourcePath {

	using Index = std::unordered_map<std::string, std::filesystem::path>;

	// 16桁の小文字hexのみで構成され、パス区切りや拡張子を含まないものをGUID参照とみなす
	inline bool IsGuidReference(const std::string& file) {

		if (file.size() != 16) {
			return false;
		}
		for (char c : file) {
			const bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
			if (!isHex) {
				return false;
			}
		}
		return true;
	}

	// .metaから "guid" の値を取り出す、JSON依存を避けて軽量に文字列抽出する
	inline std::string ReadGuidFromMeta(const std::filesystem::path& metaPath) {

		std::ifstream ifs(metaPath, std::ios::binary);
		if (!ifs) {
			return {};
		}
		std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

		const std::string key = "\"guid\"";
		size_t pos = content.find(key);
		if (pos == std::string::npos) {
			return {};
		}
		pos = content.find('"', pos + key.size());
		if (pos == std::string::npos) {
			return {};
		}
		const size_t begin = pos + 1;
		const size_t endQuote = content.find('"', begin);
		if (endQuote == std::string::npos) {
			return {};
		}
		return content.substr(begin, endQuote - begin);
	}

	// ファイル名->実体パスの索引、起動時に一度だけ構築する
	inline const Index& GetFileNameIndex() {

		static const Index index = []() {
			Index result{};
			const std::filesystem::path shaderRoot = RuntimePaths::GetEngineAssetPath("Shaders");

			std::error_code ec;
			if (!std::filesystem::exists(shaderRoot, ec) || ec || !std::filesystem::is_directory(shaderRoot, ec)) {
				return result;
			}
			for (std::filesystem::recursive_directory_iterator it(shaderRoot, ec), end; it != end && !ec; it.increment(ec)) {
				if (!it->is_regular_file(ec)) {
					continue;
				}
				const std::filesystem::path path = it->path();
				result.try_emplace(path.filename().string(), path);
			}
			return result;
		}();
		return index;
	}

	// GUID->実体パスの索引、シェーダーソースの.hlslや.hlsliの横の.metaから構築する
	inline const Index& GetGuidIndex() {

		static const Index index = []() {
			Index result{};
			const std::filesystem::path shaderRoot = RuntimePaths::GetEngineAssetPath("Shaders");

			std::error_code ec;
			if (!std::filesystem::exists(shaderRoot, ec) || ec || !std::filesystem::is_directory(shaderRoot, ec)) {
				return result;
			}
			for (std::filesystem::recursive_directory_iterator it(shaderRoot, ec), end; it != end && !ec; it.increment(ec)) {
				if (!it->is_regular_file(ec)) {
					continue;
				}
				const std::filesystem::path path = it->path();
				const std::string ext = path.extension().string();
				if (ext != ".hlsl" && ext != ".hlsli") {
					continue;
				}
				const std::filesystem::path metaPath = path.string() + ".meta";
				if (!std::filesystem::exists(metaPath, ec)) {
					continue;
				}
				const std::string guid = ReadGuidFromMeta(metaPath);
				if (!guid.empty()) {
					result.try_emplace(guid, path);
				}
			}
			return result;
		}();
		return index;
	}

	// シェーダーソース参照のGUIDまたはパスを実体パスへ解決する
	inline std::filesystem::path Resolve(const std::string& fileOrGuid) {

		if (fileOrGuid.empty()) {
			return {};
		}

		// GUID参照なら索引から引く、ファイル移動に強い
		if (IsGuidReference(fileOrGuid)) {

			const auto& guidIndex = GetGuidIndex();
			auto found = guidIndex.find(fileOrGuid);
			if (found != guidIndex.end()) {
				return found->second;
			}
			return {};
		}

		// 論理アセットパスが指定されている場合はそのまま解決
		const std::filesystem::path resolved = RuntimePaths::ResolveAssetPath(fileOrGuid);
		if (std::filesystem::exists(resolved) && std::filesystem::is_regular_file(resolved)) {
			return resolved;
		}

		// Assets/Shaders/からの相対パスを優先
		const std::filesystem::path shaderBasePath = RuntimePaths::GetEngineAssetPath("Shaders");
		std::filesystem::path direct = shaderBasePath / fileOrGuid;
		if (std::filesystem::exists(direct)) {
			return direct;
		}

		// そのまま絶対/相対で存在するなら使う
		std::filesystem::path raw(fileOrGuid);
		if (std::filesystem::exists(raw)) {
			return raw;
		}

		// ファイル名だけの指定は索引から引く
		const auto& nameIndex = GetFileNameIndex();
		auto found = nameIndex.find(std::filesystem::path(fileOrGuid).filename().string());
		if (found != nameIndex.end()) {
			return found->second;
		}
		return {};
	}
} // namespace Engine::ShaderSourcePath
