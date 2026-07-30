#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>

//============================================================================
//	ShaderSourcePathResolver
//	shader.jsonやインラインdescで指定されたシェーダーソース参照を実体パスへ解決する
//	参照は.metaのguidでもShadersルート相対パスでも受け付ける
//============================================================================
namespace Engine::ShaderSourcePath {

	using Index = std::unordered_map<std::string, std::filesystem::path>;

	// 32桁AssetGUIDを索引検索用の小文字表記へ正規化する
	inline std::optional<std::string> NormalizeGuidReference(std::string_view file) {

		const std::optional<AssetGUID> guid = TryParseAssetGUID32Hex(file);
		if (!guid) {
			return std::nullopt;
		}
		return ToString(*guid);
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
				result.try_emplace(Algorithm::PathToUTF8(path.filename()), path);
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
				std::filesystem::path metaPath = path;
				metaPath += L".meta";
				if (!std::filesystem::exists(metaPath, ec)) {
					continue;
				}
				const std::optional<std::string> guid =
					NormalizeGuidReference(ReadGuidFromMeta(metaPath));
				if (guid) {
					result.try_emplace(*guid, path);
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
		if (const std::optional<std::string> guid = NormalizeGuidReference(fileOrGuid)) {

			const auto& guidIndex = GetGuidIndex();
			auto found = guidIndex.find(*guid);
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
		std::filesystem::path direct = shaderBasePath / Algorithm::PathFromUTF8(fileOrGuid);
		if (std::filesystem::exists(direct)) {
			return direct;
		}

		// そのまま絶対/相対で存在するなら使う
		const std::filesystem::path raw = Algorithm::PathFromUTF8(fileOrGuid);
		if (std::filesystem::exists(raw)) {
			return raw;
		}

		// ファイル名だけの指定は索引から引く
		const auto& nameIndex = GetFileNameIndex();
		auto found = nameIndex.find(Algorithm::PathToUTF8(raw.filename()));
		if (found != nameIndex.end()) {
			return found->second;
		}
		return {};
	}
} // namespace Engine::ShaderSourcePath
