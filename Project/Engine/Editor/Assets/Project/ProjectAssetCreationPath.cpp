#include "ProjectAssetFileUtility.h"

//============================================================================
//	include
//============================================================================
#include "ProjectAssetDocumentFactory.h"
#include "ProjectAssetPath.h"
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <array>
#include <exception>

namespace {

	// Windowsでファイルやフォルダーに使えない名前を判定する
	bool IsInvalidPart(const std::string& name) {

		if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ' ||
			Engine::ProjectAssetPath::SanitizeFileName(name) != name ||
			std::any_of(name.begin(), name.end(), [](unsigned char value) { return value < 32; })) return true;
		const std::string stem = Engine::Algorithm::ToLower(name.substr(0, name.find('.')));
		constexpr std::array reserved{"con", "prn", "aux", "nul", "conin$", "conout$"};
		if (std::find(reserved.begin(), reserved.end(), stem) != reserved.end()) return true;
		return stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
			stem.back() >= '1' && stem.back() <= '9';
	}
}

//============================================================================
//	ProjectAssetFileUtility creationMethods
//============================================================================
Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::PlanToolAsset(
	ProjectAssetFileKind kind, const std::string& path) {

	ProjectAssetFileResult result;
	std::string relative = path;
	std::replace(relative.begin(), relative.end(), '\\', '/');
	if (Algorithm::ToLower(relative).starts_with("gameassets/")) relative.erase(0, 11);
	if (relative.empty() || relative.back() == '/') {
		result.message = "名前を入力してください";
		return result;
	}
	try {
		const auto input = Algorithm::PathFromUTF8(relative);
		if (!ProjectAssetPath::IsSafeRelativePath(input)) {
			result.message = "GameAssets内の相対パスを指定してください";
			return result;
		}
		for (const auto& part : input) {
			if (IsInvalidPart(Algorithm::PathToUTF8(part))) {
				result.message = "ファイル名またはフォルダー名が無効です";
				return result;
			}
		}
		const std::string name = Algorithm::PathToUTF8(input.filename());
		if (ProjectAssetPath::RemoveTypedSuffix(name, ProjectAssetDocumentFactory::GetFileSuffix(kind)).empty()) {
			result.message = "名前を入力してください";
			return result;
		}
		const auto root = std::filesystem::weakly_canonical(ProjectAssetPath::GetSourceRoot(ProjectAssetSource::Game));
		const auto parent = std::filesystem::weakly_canonical(root / input.parent_path());
		const auto relativeParent = parent.lexically_relative(root);
		if ((relativeParent.empty() && parent != root) || !ProjectAssetPath::IsSafeRelativePath(relativeParent)) {
			result.message = "GameAssets外には作成できません";
			return result;
		}
		const size_t separator = relative.find_last_of('/');
		const std::string directory = separator != std::string::npos ? "GameAssets/" + relative.substr(0, separator) : "GameAssets";
		return PlanCreate(ProjectAssetSource::Game, directory, kind, name, false);
	} catch (const std::exception& error) {
		result.message = "作成先を確認できません: " + std::string(error.what());
		return result;
	}
}
