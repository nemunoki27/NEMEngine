#include "ScriptFieldInspector.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedBuildDiagnosticStore.h>

// c++
#include <filesystem>

namespace Engine::ScriptFieldInspector {

	ManagedScriptResolutionReason ResolveScriptReason(const Engine::ScriptEntry& entry,
		bool sourceBuildFailed, bool globalBuildFailed) {

		if (entry.scriptTypeID.empty()) {
			return ManagedScriptResolutionReason::Unassigned;
		}
		if (sourceBuildFailed) {
			return ManagedScriptResolutionReason::BuildFailed;
		}
		const Engine::BehaviorTypeInfo* info =
			Engine::BehaviorTypeRegistry::GetInstance().FindByStableScriptTypeID(entry.scriptTypeID);
		// 未登録なら欠落扱い、値は保持して削除しない
		if (info) {
			return ManagedScriptResolutionReason::Resolved;
		}
		return globalBuildFailed ? ManagedScriptResolutionReason::BuildFailed
			: ManagedScriptResolutionReason::TypeNotRegistered;
	}

	const Engine::ManagedBuildDiagnostic* FindSourceBuildError(const Engine::ScriptEntry& entry,
		const Engine::AssetDatabase* assetDatabase, uint64_t buildID) {

		if (!assetDatabase || !entry.scriptAsset || buildID == 0) {
			return nullptr;
		}
		const std::filesystem::path scriptPath = assetDatabase->ResolveFullPath(entry.scriptAsset);
		if (scriptPath.empty()) {
			return nullptr;
		}
		for (const Engine::ManagedBuildDiagnostic& diagnostic :
			Engine::ManagedBuildDiagnosticStore::GetInstance().Entries()) {

			if (diagnostic.buildID != buildID || diagnostic.severity != Engine::DiagnosticSeverity::Error ||
				diagnostic.file.empty()) {
				continue;
			}
			std::error_code pathError{};
			if (std::filesystem::equivalent(scriptPath, std::filesystem::path(diagnostic.file), pathError) &&
				!pathError) {
				return &diagnostic;
			}
		}
		return nullptr;
	}

	const Engine::ManagedBuildDiagnostic* FindGlobalBuildError(uint64_t buildID) {

		for (const Engine::ManagedBuildDiagnostic& diagnostic :
			Engine::ManagedBuildDiagnosticStore::GetInstance().Entries()) {

			if (diagnostic.buildID == buildID && diagnostic.severity == Engine::DiagnosticSeverity::Error &&
				diagnostic.file.empty()) {
				return &diagnostic;
			}
		}
		return nullptr;
	}

	const char* ResolutionReasonLabel(ManagedScriptResolutionReason reason) {
		switch (reason) {
		case ManagedScriptResolutionReason::Resolved:          return "解決済み";
		case ManagedScriptResolutionReason::Unassigned:        return "スクリプトが未設定です";
		case ManagedScriptResolutionReason::BuildFailed:       return "スクリプトのビルドに失敗しています";
		case ManagedScriptResolutionReason::TypeNotRegistered: return "登録されている型が見つかりません";
		case ManagedScriptResolutionReason::SchemaUnavailable: return "型情報を取得できません";
		default:                                               return "原因を特定できません";
		}
	}

	std::string ScriptTypeShortName(const std::string& fullName) {

		const size_t dot = fullName.find_last_of('.');
		return dot == std::string::npos ? fullName : fullName.substr(dot + 1);
	}
}
