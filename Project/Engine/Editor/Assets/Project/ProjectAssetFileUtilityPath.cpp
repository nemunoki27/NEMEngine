#include "ProjectAssetFileUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <array>
#include <cctype>
#include <format>

//============================================================================
//	Internal Path Helpers
//============================================================================
namespace {

	constexpr std::array<const char*, 7> kCompoundSuffixes = {
		".scene.json",
		".prefab.json",
		".material.json",
		".animClip.json",
		".shader.json",
		".pipeline.json",
		".graph.json",
	};

	// 文字列前後の空白を取り除く
	std::string Trim(std::string text) {
		auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
		while (!text.empty() && isSpace(static_cast<unsigned char>(text.front()))) { text.erase(text.begin()); }
		while (!text.empty() && isSpace(static_cast<unsigned char>(text.back()))) { text.pop_back(); }
		return text;
	}

} // namespace

namespace Engine {

	std::string ProjectAssetFileUtility::SanitizeFileName(std::string text) {
		text = Trim(std::move(text));
		// OSで禁止されている、または問題になりやすい文字をアンダースコアに置換
		for (char& c : text) {
			switch (c) {
			case '<': case '>': case ':': case '"': case '/': case '\\': case '|': case '?': case '*':
				c = '_';
				break;
			}
		}
		// 空文字列や予約済みディレクトリ名は避ける
		if (text.empty() || text == "." || text == "..") { return "NewAsset"; }
		return text;
	}

	std::string ProjectAssetFileUtility::MakeCSharpClassName(const std::string& fileName) {
		std::string result;
		bool upperNext = true;
		for (unsigned char c : fileName) {
			// アルファベット、数字、アンダースコア以外は無視してPascalCaseに変換を試みる
			if (std::isalnum(c) || c == '_') {
				char out = static_cast<char>(c);
				if (upperNext && std::isalpha(c)) { out = static_cast<char>(std::toupper(c)); }
				result.push_back(out);
				upperNext = false;
			}
			else { upperNext = true; }
		}
		// 数字で始まる名前はC#のクラス名として無効なためプレフィックスを付与
		if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front()))) { result = "New" + result; }
		return result;
	}

	std::pair<std::string, std::string> ProjectAssetFileUtility::SplitAssetFileName(const std::filesystem::path& path) {
		const std::string fileName = path.filename().string();
		const std::string lower = Engine::Algorithm::ToLower(fileName);

		// エンジン独自の複合拡張子（.scene.jsonなど）を優先的に判定
		for (const char* suffix : kCompoundSuffixes) {
			const std::string suffixText = suffix;
			if (Engine::Algorithm::EndsWith(lower, Engine::Algorithm::ToLower(suffixText))) {
				const size_t suffixSize = suffixText.size();
				return { fileName.substr(0, fileName.size() - suffixSize), fileName.substr(fileName.size() - suffixSize) };
			}
		}
		// 複合拡張子に該当しない場合は標準のstem/extensionを使用
		return { path.stem().string(), path.extension().string() };
	}

	std::filesystem::path ProjectAssetFileUtility::MakeUniquePath(const std::filesystem::path& preferredPath) {
		// ファイルが既に存在する場合のみ、連番を付与して重複を避ける
		if (!std::filesystem::exists(preferredPath)) { return preferredPath; }
		const auto [baseName, suffix] = SplitAssetFileName(preferredPath);
		const std::filesystem::path directory = preferredPath.parent_path();
		for (uint32_t index = 1; index < 10000; ++index) {
			std::filesystem::path candidate = directory / std::format("{} {}{}", baseName, index, suffix);
			if (!std::filesystem::exists(candidate)) { return candidate; }
		}
		return {};
	}

	std::string ProjectAssetFileUtility::RemoveTypedSuffix(std::string name, const char* suffix) {
		const std::string lowerName = Engine::Algorithm::ToLower(name);
		const std::string lowerSuffix = Engine::Algorithm::ToLower(suffix);
		// 指定されたサフィックスが末尾にある場合のみ削除
		if (!lowerSuffix.empty() && Engine::Algorithm::EndsWith(lowerName, lowerSuffix)) {
			name.resize(name.size() - lowerSuffix.size());
		}
		return name;
	}

	bool ProjectAssetFileUtility::IsSafeRelativePath(const std::filesystem::path& path) {
		// ディレクトリ階層を上る指定が含まれている場合は危険と判断
		if (path.empty()) { return true; }
		for (const auto& part : path) { if (part == "..") { return false; } }
		return true;
	}

	std::string ProjectAssetFileUtility::ToAssetPath(const std::filesystem::path& fullPath) {
		return Engine::RuntimePaths::ToAssetPath(fullPath);
	}

	std::filesystem::path ProjectAssetFileUtility::GetSourceRoot(ProjectAssetSource source) {
		switch (source) {
		case ProjectAssetSource::Engine: return RuntimePaths::GetEngineAssetsRoot();
		case ProjectAssetSource::Game: return RuntimePaths::GetGameRoot() / "GameAssets";
		}
		// デフォルトはエンジンアセット
		return RuntimePaths::GetEngineAssetsRoot();
	}

	std::filesystem::path ProjectAssetFileUtility::ResolveVirtualDirectory(ProjectAssetSource source, const std::string& directoryVirtualPath) {
		const std::filesystem::path root = GetSourceRoot(source);
		const std::string virtualRoot = GetSourceVirtualRoot(source);
		if (directoryVirtualPath == virtualRoot) { return root; }
		const std::string prefix = virtualRoot + "/";
		// 仮想パスがルート配下でないなら無効
		if (directoryVirtualPath.rfind(prefix, 0) != 0) { return {}; }
		const std::filesystem::path relative = directoryVirtualPath.substr(prefix.size());
		// 親階層への移動(..)を含むパスはセキュリティのため制限
		if (!IsSafeRelativePath(relative)) { return {}; }
		return (root / relative).lexically_normal();
	}

	const char* ProjectAssetFileUtility::GetSourceVirtualRoot(ProjectAssetSource source) {
		switch (source) {
		case ProjectAssetSource::Engine: return "Engine/Assets";
		case ProjectAssetSource::Game: return "GameAssets";
		}
		return "Engine/Assets";
	}

	bool ProjectAssetFileUtility::IsSameOrChildPath(const std::filesystem::path& path, const std::filesystem::path& parent) {
		const std::filesystem::path normalizedPath = path.lexically_normal();
		const std::filesystem::path normalizedParent = parent.lexically_normal();
		auto pathIt = normalizedPath.begin();
		auto parentIt = normalizedParent.begin();
		// parentの全パス要素がpathの先頭部分と一致するかを走査
		for (; parentIt != normalizedParent.end(); ++parentIt, ++pathIt) {
			if (pathIt == normalizedPath.end() || *pathIt != *parentIt) { return false; }
		}
		return true;
	}

	std::filesystem::path ProjectAssetFileUtility::MakeMetaPath(const std::filesystem::path& path) {
		// アセットファイル名に .meta を付与してメタデータパスを作成
		return path.string() + ".meta";
	}

} // Engine
