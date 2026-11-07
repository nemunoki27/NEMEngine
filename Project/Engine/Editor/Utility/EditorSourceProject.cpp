#include "EditorSourceNavigation.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <system_error>

//============================================================================
//	EditorSourceNavigation namespaceMethods
//============================================================================
bool Engine::EditorSourceNavigation::IsEngineSourceProject() {

	// ソース開発用のプロジェクトとテンプレート作業領域だけを許可する
	const auto paths = RuntimePaths::GetSnapshot();
	std::error_code ec;
	if (paths->installedEngine) return false;
	const auto isWithin = [&](const std::filesystem::path& root) {
		const auto relative = std::filesystem::relative(paths->projectRoot, root, ec);
		if (ec || relative.empty() || relative.has_root_path()) return false;
		for (const auto& part : relative) {
			if (part == "..") return false;
		}
		return true;
	};
	if (!isWithin(paths->engineProjectRoot) &&
		!isWithin(paths->engineProjectRoot.parent_path() / "Generated/TemplateAuthoring")) return false;

	// SDKには同梱しない編集用ソースの存在も確認する
	return std::filesystem::is_regular_file(paths->engineProjectRoot /
		"Engine/Editor/UI/Inspectors/Builtin/BuiltinComponentEditorRegistration.cpp", ec);
}
