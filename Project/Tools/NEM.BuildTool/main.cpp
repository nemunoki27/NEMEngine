//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSemanticMerge.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFileJournal.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/Shaders/ShaderCook.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>

// c++
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

	int ValidateProject(const std::filesystem::path& projectPath) {

		std::error_code ec;
		std::filesystem::current_path(projectPath, ec);
		if (ec) {
			std::cerr << "プロジェクトの作業フォルダーを設定できません\n";
			return 2;
		}

		Engine::RuntimePaths::Refresh();
		Engine::AssetDatabase database;
		if (!database.Init() || !database.RebuildMeta()) {
			std::cerr << "アセットデータベースを構築できません\n";
			return 3;
		}

		for (const Engine::PackageResolveIssue& issue : Engine::RuntimePaths::GetPackageIssues()) {
			std::cerr << issue.packageName << ": " << issue.detail << '\n';
		}
		for (const Engine::AssetDatabaseIssue& issue : database.GetIssues()) {
			std::cerr << issue.assetPath << ": " << issue.detail << '\n';
		}
		return Engine::RuntimePaths::GetPackageIssues().empty() &&
			database.GetIssues().empty() ? 0 : 4;
	}

	bool CollectScenePaths(const std::filesystem::path& root, std::vector<std::filesystem::path>& paths) {

		std::error_code ec;
		auto it = std::filesystem::recursive_directory_iterator(root, ec);
		while (!ec && it != std::filesystem::recursive_directory_iterator{}) {

			// 列挙失敗を対象なしとして扱わない
			if (it->is_regular_file(ec) &&
				Engine::AssetTypeResolver::GuessByPath(it->path()) == Engine::AssetType::Scene) {
				paths.push_back(it->path());
			}
			if (ec) break;
			it.increment(ec);
		}
		if (ec) std::cerr << "シーンの列挙に失敗しました: " << root << " / " << ec.message() << '\n';
		return !ec;
	}

	int CanonicalizeScenes(const std::filesystem::path& projectPath, bool includeEngine) {

		std::error_code ec;
		std::filesystem::current_path(projectPath, ec);
		if (ec) {
			std::cerr << "プロジェクトの作業フォルダーを設定できません\n";
			return 2;
		}
		Engine::RuntimePaths::Refresh();

		std::vector<std::filesystem::path> paths;
		if (!CollectScenePaths(Engine::RuntimePaths::GetGameAssetsRoot(), paths)) {
			return 5;
		}
		if (includeEngine &&
			!CollectScenePaths(Engine::RuntimePaths::GetEngineAssetsRoot(), paths)) {
			return 5;
		}
		Engine::SceneAssetStorage storage;
		std::string error;
		if (!storage.Canonicalize(paths, error)) {
			std::cerr << error << '\n';
			return 5;
		}
		std::cout << "正規化したシーン数: " << paths.size() << '\n';
		return 0;
	}

	Engine::JsonFileJournal::Scope MakeMergeScope(const std::filesystem::path& outputPath) {

		const auto outputKey = Engine::StorageFileUtility::PathKey(outputPath);
		auto reportPath = outputPath;
		reportPath += L".merge-conflicts.json";
		const auto reportKey = Engine::StorageFileUtility::PathKey(reportPath);
		return {
			std::filesystem::absolute(outputPath).parent_path() / ".NEMMergeRecovery",
			[outputKey, reportKey](const std::filesystem::path& path) {
				const auto key = Engine::StorageFileUtility::PathKey(path);
				return key == outputKey || key == reportKey;
			}
		};
	}

	int RecoverMergeJson(const std::filesystem::path& outputPath, const std::filesystem::path& recovery) {

		try {
			std::string error;
			if (!Engine::JsonFileJournal::Recover(MakeMergeScope(outputPath), recovery, error, [](const std::filesystem::path&) {})) {
				std::cerr << "マージ結果を復旧できません: " << error << '\n';
				return 6;
			}
			return 0;
		} catch (const std::exception& exception) {
			std::cerr << "マージ結果を復旧できません: " << exception.what() << '\n';
			return 6;
		}
	}

	int MergeJsonFiles(const std::filesystem::path& basePath,
		const std::filesystem::path& ourPath,
		const std::filesystem::path& theirPath,
		const std::filesystem::path& outputPath) {

		try {
			nlohmann::json base, ours, theirs;
			if (!Engine::JsonAdapter::TryLoad(basePath, base) || !Engine::JsonAdapter::TryLoad(ourPath, ours) ||
				!Engine::JsonAdapter::TryLoad(theirPath, theirs)) {
				std::cerr << "マージ元ファイルを読み込めません\n";
				return 6;
			}
			const auto result = Engine::JsonSemanticMerge::Merge(base, ours, theirs);
			std::filesystem::path conflictPath = outputPath;
			conflictPath += L".merge-conflicts.json";
			nlohmann::json report = {{ "conflicts", nlohmann::json::array() }};
			for (const auto& conflict : result.conflicts) {
				report["conflicts"].push_back({{ "path", conflict.path }, { "base", conflict.base },
					{ "ours", conflict.ours }, { "theirs", conflict.theirs }});
			}

			// 2文書の退避と準備が終わってから結果を公開する
			const auto scope = MakeMergeScope(outputPath);
			const std::vector<Engine::JsonFileChange> changes{
				{ conflictPath, report, result.Succeeded() }, { outputPath, result.merged, false }
			};
			std::string error;
			if (!Engine::JsonFileJournal::Commit(scope, changes, "JSON Merge", error,
				[&scope](const std::filesystem::path& directory, std::string& rollbackError) {
					return Engine::JsonFileJournal::Recover(scope, directory, rollbackError, [](const std::filesystem::path&) {});
				})) {
				std::cerr << "マージ結果を保存できません: " << error << '\n';
				return 6;
			}
			if (result.Succeeded()) return 0;
			std::cerr << "構造化マージの競合数: " << result.conflicts.size() << '\n';
			return 7;
		} catch (const std::exception& error) {
			std::cerr << "マージ処理に失敗しました: " << error.what() << '\n';
			return 6;
		}
	}

	int VerifyCook(const std::filesystem::path& manifestPath,
		const std::filesystem::path& contentRoot) {

		const nlohmann::json manifest =
			Engine::JsonAdapter::Load(manifestPath);
		if (!manifest.is_object() ||
			manifest.value("schemaVersion", 0) != 1 ||
			!manifest.contains("files") || !manifest["files"].is_array()) {
			std::cerr << "Cookマニフェストが不正です\n";
			return 9;
		}

		std::error_code ec;
		const std::filesystem::path normalizedRoot =
			std::filesystem::weakly_canonical(contentRoot, ec);
		if (ec || !std::filesystem::is_directory(normalizedRoot, ec)) {
			std::cerr << "Cookの検証対象フォルダーが見つかりません\n";
			return 9;
		}

		size_t fileCount = 0;
		for (const nlohmann::json& entry : manifest["files"]) {

			const std::filesystem::path relative =
				Engine::Algorithm::PathFromUTF8(
					entry.value("path", std::string{}));
			if (relative.empty() || relative.is_absolute()) {
				return 9;
			}
			const std::filesystem::path fullPath =
				(normalizedRoot / relative).lexically_normal();
			const std::filesystem::path relativeCheck =
				fullPath.lexically_relative(normalizedRoot);
			if (relativeCheck.empty() ||
				relativeCheck.native().starts_with(L"..") ||
				!std::filesystem::is_regular_file(fullPath, ec) ||
				std::filesystem::file_size(fullPath, ec) !=
				entry.value("size", uintmax_t{ 0 }) ||
				Engine::ContentHash::FileSHA256(fullPath) !=
				entry.value("sha256", std::string{})) {

				std::cerr << "Cookの検証に失敗しました: " <<
					Engine::Algorithm::PathToUTF8(relative) << '\n';
				return 10;
			}
			++fileCount;
		}
		std::cout << "Cookの検証が完了しました: " << fileCount << "ファイル\n";
		return 0;
	}

	int CookShaders(const std::filesystem::path& manifestPath,
		const std::filesystem::path& outputRoot) {

		Engine::ShaderCookResult result{};
		std::string error;
		if (!Engine::ShaderCook::Cook(
			manifestPath, outputRoot, result, error)) {
			std::cerr << "[シェーダーCook] " << error << '\n';
			return 11;
		}
		std::cout << "[シェーダーCook] シェーダー数=" << result.shaderCount <<
			" ステージ数=" << result.stageCount <<
			" DXILバイト数=" << result.bytecodeSize << '\n';
		return 0;
	}
}

int main(int argc, char** argv) {

	if (argc == 3 && std::string_view(argv[1]) == "--validate-project") {
		return ValidateProject(std::filesystem::path(argv[2]));
	}
	if (argc == 3 && std::string_view(argv[1]) == "--canonicalize-scenes") {
		return CanonicalizeScenes(std::filesystem::path(argv[2]), false);
	}
	if (argc == 4 && std::string_view(argv[1]) == "--canonicalize-scenes" &&
		std::string_view(argv[3]) == "--include-engine") {
		return CanonicalizeScenes(std::filesystem::path(argv[2]), true);
	}
	if (argc == 4 && std::string_view(argv[1]) == "--recover-json-merge") {
		return RecoverMergeJson(argv[2], argv[3]);
	}
	if (argc == 6 && std::string_view(argv[1]) == "--merge-json") {
		return MergeJsonFiles(argv[2], argv[3], argv[4], argv[5]);
	}
	if (argc == 4 && std::string_view(argv[1]) == "--verify-cook") {
		return VerifyCook(argv[2], argv[3]);
	}
	if (argc == 4 && std::string_view(argv[1]) == "--cook-shaders") {
		return CookShaders(argv[2], argv[3]);
	}

	std::cout << "使い方:\nNEMBuildTool --validate-project <プロジェクトフォルダー>\n"
		"NEMBuildTool --canonicalize-scenes <プロジェクトフォルダー> [--include-engine]\n"
		"NEMBuildTool --merge-json <共通祖先> <自分側> <相手側> <出力先>\n"
		"NEMBuildTool --recover-json-merge <出力先> <復旧記録ディレクトリ>\n"
		"NEMBuildTool --verify-cook <マニフェスト> <検証対象フォルダー>\n"
		"NEMBuildTool --cook-shaders <製品ビルドマニフェスト> <出力先>\n";
	return argc == 1 ? 0 : 1;
}
