#include "GameBuildUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <array>
#include <fstream>

namespace Engine::GameBuildUtility {

	// 製品名として使用できないWindows予約名か
	bool IsReservedWindowsName(const std::string& name) {

		const std::string lower = Engine::Algorithm::ToLower(name);
		const size_t extension = lower.find('.');
		const std::string baseName = lower.substr(0, extension);
		static const std::array<const char*, 22> kReserved = {
			"con", "prn", "aux", "nul",
			"com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
			"lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
		};
		return std::find(kReserved.begin(), kReserved.end(), baseName) != kReserved.end();
	}

	// Exe入力から拡張子を除いた製品名を取得
	bool ResolveProductName(const std::string& input, std::string& outName, std::string& outError) {

		outName = input;
		if (Engine::Algorithm::EndsWith(Engine::Algorithm::ToLower(outName), ".exe")) {
			outName.resize(outName.size() - 4);
		}
		if (outName.empty()) {
			outError = "Exeの名前を入力してください";
			return false;
		}
		if (outName.find_first_of("<>:\"/\\|?*") != std::string::npos ||
			outName.back() == '.' || outName.back() == ' ') {
			outError = "Exeの名前に使用できない文字が含まれています";
			return false;
		}
		if (IsReservedWindowsName(outName)) {
			outError = "Windowsの予約名はExeの名前に使用できません";
			return false;
		}
		return true;
	}

	// JSONファイルを例外なしで読み込む
	nlohmann::json LoadJson(const std::filesystem::path& path) {

		std::ifstream file(path, std::ios::binary);
		if (!file.is_open()) {
			return {};
		}
		const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		return nlohmann::json::parse(content, nullptr, false);
	}

	// パスがprefixから始まるか
	bool StartsWith(const std::string& text, const char* prefix) {

		return text.rfind(prefix, 0) == 0;
	}

	std::filesystem::path NormalizeBuildPath(const std::filesystem::path& path) {

		std::error_code ec;
		const std::filesystem::path normalized =
			std::filesystem::weakly_canonical(path, ec);
		return ec ? path.lexically_normal() : normalized;
	}

	// ゲームの生成物を持つワークスペースルートを取得
	std::filesystem::path ResolveGameBuildRoot(const std::filesystem::path& gameRoot) {

		std::error_code ec;
		for (std::filesystem::path current = gameRoot; !current.empty(); current = current.parent_path()) {

			if (std::filesystem::is_regular_file(current / "Premake/premake5.lua", ec)) {
				return current;
			}
			ec.clear();
			if (current == current.parent_path()) {
				break;
			}
		}
		return Engine::RuntimePaths::GetProjectRoot().parent_path();
	}

	// エンジンソースとSDKの両方から製品ビルドスクリプトを探索
	std::filesystem::path ResolveGameBuildScript(const std::filesystem::path& buildRoot) {

		const std::filesystem::path& engineProjectRoot = Engine::RuntimePaths::GetEngineProjectRoot();
		const std::array candidates = {
			buildRoot / "Tools/BuildGame.ps1",
			engineProjectRoot / "Tools/BuildGame.ps1",
			engineProjectRoot.parent_path() / "Tools/BuildGame.ps1",
		};
		std::error_code ec;
		for (const std::filesystem::path& candidate : candidates) {

			if (std::filesystem::is_regular_file(candidate, ec)) {
				return candidate;
			}
			ec.clear();
		}
		return engineProjectRoot / "Tools/BuildGame.ps1";
	}

	// 製品へ含めないエディター専用アセットか
	bool IsEditorOnlyAsset(const std::string& assetPath) {

		return StartsWith(assetPath, "Engine/Assets/Textures/Editor/") ||
			StartsWith(assetPath, "Engine/Assets/Shaders/Builtin/Editor/") ||
			(StartsWith(assetPath, "Engine/Assets/Config/") &&
				assetPath != "Engine/Assets/Config/windowSettings.exeConfig.json" &&
				assetPath != "Engine/Assets/Config/windowSettings.exeConfig.json.meta");
	}

	// 製品実行では使用しないGameAssets内の編集用ファイルか
	bool IsGameEditorOnlyAsset(const std::string& assetPath) {

		std::string lower = Engine::Algorithm::ToLower(assetPath);
		if (!StartsWith(lower, "gameassets/")) {
			return false;
	}
		if (StartsWith(lower, "gameassets/fonts/charset/")) {
			return true;
		}
		if (Engine::Algorithm::EndsWith(lower, ".merge-conflicts.json") ||
			Engine::Algorithm::EndsWith(lower, ".tmp") ||
			Engine::Algorithm::EndsWith(lower, ".bak") ||
			Engine::Algorithm::EndsWith(lower, "thumbs.db") ||
			Engine::Algorithm::EndsWith(lower, ".ds_store")) {
			return true;
		}
		if (Engine::Algorithm::EndsWith(lower, ".meta")) {
			lower.resize(lower.size() - 5);
		}
		return Engine::Algorithm::EndsWith(lower, ".cs") ||
			Engine::Algorithm::EndsWith(lower, ".ttf") ||
			Engine::Algorithm::EndsWith(lower, ".otf");
	}

}
