#pragma once

//============================================================================
//	include
//============================================================================
#include <filesystem>
#include <string>

namespace Engine {

	//============================================================================
	//	ManagedIdeSettings struct
	//	ProjectSettingsのスクリプト構成とUserSettingsのIDE設定
	//============================================================================
	struct ManagedIdeSettings {

		// VisualStudioはインストール済みdevenv.exeを探して/Editで開く、既定で従来挙動
		// SystemDefaultはOS既定の関連付けでfileを開く、lineとcolumnは無視する
		// Executableはexecutableとargumentsのtoken展開で起動する
		std::string mode = "VisualStudio";
		std::string executable;
		// tokenは{project} {file} {line} {column}
		std::string arguments = "{file}";
		// {project}に展開するcsproj相対path、GameRoot基準
		std::string project = "Scripts/GameScripts.csproj";
	};

	//============================================================================
	//	ManagedIdeLauncher
	//	ProjectPanelのcs openとCompiler Error ListのjumpとScript exceptionのstack jumpで共通利用するIDE起動
	//============================================================================
	namespace ManagedIdeLauncher {

		// プロジェクト構成とIDE設定を読み直す、無ければ既定値
		void ReloadSettings();
		// 現在の設定を返す、読み込み済みでなければloadする
		const ManagedIdeSettings& GetSettings();

		// fileをIDEで開く、lineとcolumnは対応モードのみ使いSystemDefaultはfileのみのfallback
		// 成功でtrue、missing executableやmissing projectやlaunch失敗はfalseでdiagnosticに記録する
		bool OpenFile(const std::filesystem::path& file, int32_t line = 0, int32_t column = 0);
	}
} // Engine
