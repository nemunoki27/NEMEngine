#include "ProjectAssetPath.h"

//============================================================================
//	include
//============================================================================

#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <cctype>
#include <format>

//============================================================================
//	Internal Path Helpers
//============================================================================
namespace {

	// 文字列前後の空白を取り除く
	std::string Trim(std::string text) {
		auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
		while (!text.empty() && isSpace(static_cast<unsigned char>(text.front()))) {
			text.erase(text.begin());
		}
		while (!text.empty() && isSpace(static_cast<unsigned char>(text.back()))) {
			text.pop_back();
		}
		return text;
	}

} // namespace

namespace Engine {

	std::string ProjectAssetPath::SanitizeFileName(std::string text) {
		text = Trim(std::move(text));
		// OSで禁止されている、または問題になりやすい文字をアンダースコアに置換
		for (char& c : text) {
			switch (c) {
			case '<':
			case '>':
			case ':':
			case '"':
			case '/':
			case '\\':
			case '|':
			case '?':
			case '*':
				c = '_';
				break;
			}
		}
		// 空文字列や予約済みディレクトリ名は避ける
		if (text.empty() || text == "." || text == "..") {
			return "NewAsset";
		}
		return text;
	}

	std::string ProjectAssetPath::MakeCSharpClassName(const std::string& fileName) {
		std::string result;
		bool upperNext = true;
		for (unsigned char c : fileName) {
			// アルファベット、数字、アンダースコア以外は無視してPascalCaseに変換を試みる
			if (std::isalnum(c) || c == '_') {
				char out = static_cast<char>(c);
				if (upperNext && std::isalpha(c)) {
					out = static_cast<char>(std::toupper(c));
				}
				result.push_back(out);
				upperNext = false;
			} else {
				upperNext = true;
			}
		}
		// 数字で始まる名前はC#のクラス名として無効なためプレフィックスを付与
		if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front()))) {
			result = "New" + result;
		}
		return result;
	}

	std::pair<std::string, std::string> ProjectAssetPath::SplitAssetFileName(const std::filesystem::path& path) {
		const std::string fileName = Engine::Algorithm::PathToUTF8(path.filename());

		// .scene.jsonなどのエンジン独自の複合拡張子を優先的に判定
		const std::string_view suffix = AssetTypeResolver::FindCompoundSuffix(path);
		if (!suffix.empty()) {
			const size_t suffixSize = suffix.size();
			return {fileName.substr(0, fileName.size() - suffixSize), fileName.substr(fileName.size() - suffixSize)};
		}
		// 複合拡張子に該当しない場合は標準のstem/extensionを使用
		return {Engine::Algorithm::PathToUTF8(path.stem()), Engine::Algorithm::PathToUTF8(path.extension())};
	}

	std::filesystem::path ProjectAssetPath::MakeUniquePath(const std::filesystem::path& preferredPath) {
		// 孤立metaが持つ別Assetの識別子も引き継がない
		const auto available = [](const std::filesystem::path& path) {
			return !std::filesystem::exists(path) && !std::filesystem::exists(MakeMetaPath(path));
		};
		if (available(preferredPath)) {
			return preferredPath;
		}
		const auto [baseName, suffix] = SplitAssetFileName(preferredPath);
		const std::filesystem::path directory = preferredPath.parent_path();
		for (uint32_t index = 1; index < 10000; ++index) {
			const std::string candidateName = std::format("{} {}{}", baseName, index, suffix);
			std::filesystem::path candidate = directory / Engine::Algorithm::PathFromUTF8(candidateName);
			if (available(candidate)) {
				return candidate;
			}
		}
		return {};
	}

	std::string ProjectAssetPath::RemoveTypedSuffix(std::string name, const char* suffix) {
		const std::string lowerName = Engine::Algorithm::ToLower(name);
		const std::string lowerSuffix = Engine::Algorithm::ToLower(suffix);
		// 指定されたサフィックスが末尾にある場合のみ削除
		if (!lowerSuffix.empty() && Engine::Algorithm::EndsWith(lowerName, lowerSuffix)) {
			name.resize(name.size() - lowerSuffix.size());
		}
		return name;
	}

	bool ProjectAssetPath::IsSafeRelativePath(const std::filesystem::path& path) {
		// 絶対指定とドライブ付き指定を拒否する
		if (path.has_root_path()) {
			return false;
		}
		if (path.empty()) {
			return true;
		}
		for (const auto& part : path) {
			if (part == "..") {
				return false;
			}
		}
		return true;
	}

	std::string ProjectAssetPath::ToAssetPath(const std::filesystem::path& fullPath) {
		return Engine::RuntimePaths::ToAssetPath(fullPath);
	}

	std::filesystem::path ProjectAssetPath::GetSourceRoot(ProjectAssetSource source) {
		switch (source) {
		case ProjectAssetSource::Engine:
			return RuntimePaths::GetEngineAssetsRoot();
		case ProjectAssetSource::Game:
			return RuntimePaths::GetGameRoot() / "GameAssets";
		}
		// デフォルトはエンジンアセット
		return RuntimePaths::GetEngineAssetsRoot();
	}

	std::filesystem::path ProjectAssetPath::ResolveVirtualDirectory(
		ProjectAssetSource source, const std::string& directoryVirtualPath) {
		const std::filesystem::path root = GetSourceRoot(source);
		const std::string virtualRoot = GetSourceVirtualRoot(source);
		if (directoryVirtualPath == virtualRoot) {
			return root;
		}
		const std::string prefix = virtualRoot + "/";
		// 仮想パスがルート配下でないなら無効
		if (directoryVirtualPath.rfind(prefix, 0) != 0) {
			return {};
		}
		const std::filesystem::path relative = Engine::Algorithm::PathFromUTF8(directoryVirtualPath.substr(prefix.size()));
		// ルートの外へ出る相対指定を拒否する
		if (!IsSafeRelativePath(relative)) {
			return {};
		}
		return (root / relative).lexically_normal();
	}

	const char* ProjectAssetPath::GetSourceVirtualRoot(ProjectAssetSource source) {
		switch (source) {
		case ProjectAssetSource::Engine:
			return "Engine/Assets";
		case ProjectAssetSource::Game:
			return "GameAssets";
		}
		return "Engine/Assets";
	}

	bool ProjectAssetPath::IsSameOrChildPath(const std::filesystem::path& path, const std::filesystem::path& parent) {
		const std::filesystem::path normalizedPath = path.lexically_normal();
		const std::filesystem::path normalizedParent = parent.lexically_normal();
		auto pathIt = normalizedPath.begin();
		auto parentIt = normalizedParent.begin();
		// parentの全パス要素がpathの先頭部分と一致するかを走査
		for (; parentIt != normalizedParent.end(); ++parentIt, ++pathIt) {
			if (pathIt == normalizedPath.end() || *pathIt != *parentIt) {
				return false;
			}
		}
		return true;
	}

	std::filesystem::path ProjectAssetPath::MakeMetaPath(const std::filesystem::path& path) {
		// アセットファイル名に.metaを付与してメタデータパスを作成
		std::filesystem::path metaPath = path;
		metaPath += L".meta";
		return metaPath;
	}

} // Engine

std::filesystem::path Engine::ProjectAssetPath::MakeSiblingPath(
	const std::filesystem::path& targetPath, const std::filesystem::path& extension) {

	// 元のstemへ付随ファイルの拡張子を付ける
	std::filesystem::path result = targetPath.parent_path() / targetPath.stem();
	result += extension;
	return result;
}
