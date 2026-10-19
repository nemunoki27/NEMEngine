#include "EditorSourceNavigation.h"

//============================================================================
//	include
//============================================================================
#include "VisualStudioLauncher.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <system_error>

//============================================================================
//	EditorSourceNavigation namespaceMethods
//============================================================================
bool Engine::EditorSourceNavigation::IsEngineSourceProject() {

	// Sandboxを含むソース配下のプロジェクトだけを許可する
	const auto paths = RuntimePaths::GetSnapshot();
	std::error_code ec;
	const auto relative = std::filesystem::relative(paths->projectRoot, paths->engineProjectRoot, ec);
	if (ec || relative.empty() || relative.has_root_path()) {
		return false;
	}
	for (const auto& part : relative) {
		if (part == "..") {
			return false;
		}
	}

	// SDKには同梱しない編集用ソースの存在も確認する
	return std::filesystem::is_regular_file(paths->engineProjectRoot /
		"Engine/Editor/UI/Inspectors/Builtin/BuiltinComponentEditorRegistration.cpp", ec);
}

std::filesystem::path Engine::EditorSourceNavigation::ResolveScriptFile(
	const ScriptEntry& entry, const AssetDatabase* database) {

	if (!database) {
		return {};
	}
	std::filesystem::path source;
	if (entry.scriptAsset) {

		// 明示された参照が欠損していても別のAssetへ接続しない
		const auto* meta = database->Find(entry.scriptAsset);
		if (!meta || meta->type != AssetType::Script) {
			return {};
		}
		source = database->ResolveFullPath(entry.scriptAsset);
	} else {

		// Asset未設定時だけ安定IDに対応する定義元を使用する
		const auto* info = BehaviorTypeRegistry::GetInstance().FindByStableScriptTypeID(entry.scriptTypeID);
		if (!info || !info->managed || info->sourcePath.empty()) {
			return {};
		}
		source = RuntimePaths::ResolveAssetPath(Algorithm::PathFromUTF8(info->sourcePath));
		const auto* meta = database->FindByPath(RuntimePaths::ToAssetPath(source));
		if (!meta || meta->type != AssetType::Script) {
			return {};
		}
	}
	std::error_code ec;
	if (source.extension() != ".cs" || !std::filesystem::is_regular_file(source, ec)) {
		return {};
	}
	return source;
}

std::filesystem::path Engine::EditorSourceNavigation::ResolveComponentEditorFile(const std::string& sourceFile) {

	if (!IsEngineSourceProject()) {
		return {};
	}
	const auto relative = Algorithm::PathFromUTF8(sourceFile);
	if (relative.has_root_path() || relative.extension() != ".cpp") {
		return {};
	}
	for (const auto& part : relative) {
		if (part == "..") {
			return {};
		}
	}

	// 起動中のEngineルートを使い、リポジトリの移動にも追従する
	const auto source = RuntimePaths::GetEngineProjectRoot() / "Engine/Editor" / relative;
	std::error_code ec;
	return std::filesystem::is_regular_file(source, ec) ? source : std::filesystem::path{};
}

bool Engine::EditorSourceNavigation::OpenScript(const ScriptEntry& entry, const AssetDatabase* database) {

	const auto source = ResolveScriptFile(entry, database);
	if (source.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"[EditorSourceNavigation] スクリプトの定義元が見つかりません: {}", entry.lastKnownTypeName);
		return false;
	}
	return VisualStudioLauncher::OpenFile(source);
}

bool Engine::EditorSourceNavigation::OpenComponentEditor(const std::string& sourceFile) {

	// メニュー表示後に実行環境が変わっていても再確認する
	const auto source = ResolveComponentEditorFile(sourceFile);
	if (source.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"[EditorSourceNavigation] コンポーネントの編集元を開けません: {}", sourceFile);
		return false;
	}
	return VisualStudioLauncher::OpenFile(source);
}
