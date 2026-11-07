#pragma once

//============================================================================
//	include
//============================================================================
#include "EditorTemplateTypes.h"
#include "EditorDistributionSession.h"
#include <Engine/Editor/Utility/EditorShell.h>

// c++
#include <future>

namespace Engine {

	struct EditorPanelContext;

	//============================================================================
//	EditorTemplateMenuSession class
//	テンプレートの保存入力と独立した作業プロジェクトを管理する
//============================================================================
	class EditorTemplateMenuSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 開発者用の保存と読み込みメニューを表示する
		void DrawMenu(const EditorPanelContext& context);
		// 保存と配布管理の入力を表示する
		void DrawPopup(const EditorPanelContext& context);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		EditorTemplateEntry draft_;
		std::vector<EditorTemplateEntry> samples_;
		std::future<EditorTemplateResult> saving_;
		EditorShell::PathSelectionDialog additionalDialog_;
		EditorDistributionSession distribution_;
		std::string additionalPath_;
		std::string message_;
		bool openSave_ = false;
		bool waitingForSceneSave_ = false;
		bool failed_ = false;

		//--------- functions ----------------------------------------------------

		// 現在のシーンを保存する入力を開始する
		void Prepare(const EditorPanelContext& context, const std::string& kind);
		// 保存されたシーンから非同期の収集を開始する
		void StartSave(const EditorPanelContext& context);
		// 完了した処理とファイル選択を取り込む
		void Update(const EditorPanelContext& context);
		// プロジェクト内の追加パスを設定する
		void AddPath(const std::filesystem::path& path);
		// 保存先と動的な参照対象を編集する
		void DrawSaveFields();
	};
}
