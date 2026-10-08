#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <filesystem>
#include <string>

namespace Engine {

	//============================================================================
	//	ManagedIDESettings struct
	//	ProjectSettingsのスクリプト構成とUserSettingsのIDE設定
	//============================================================================
	struct ManagedIDESettings {

		// VisualStudio、OSの関連付け、指定EXEから起動方式を選択
		std::string mode = "VisualStudio";
		std::string executable;
		// tokenは{project} {file} {line} {column}
		std::string arguments = "{file}";
		// {project}に展開するcsproj相対path、GameRoot基準
		std::string project = "Scripts/GameScripts.csproj";
	};

	//============================================================================
	//	ManagedIDELauncher
	//	Scriptと診断の位置をIDEで開く
	//============================================================================
	namespace ManagedIDELauncher {

		// プロジェクト構成とIDE設定を読み直す、無ければ既定値
		void ReloadSettings();
		// 設定が未取得なら読み込んで返す
		const ManagedIDESettings& GetSettings();

		// ファイルと対応する行位置をIDEで開く
		bool OpenFile(const std::filesystem::path& file, int32_t line = 0, int32_t column = 0);
	}
}
