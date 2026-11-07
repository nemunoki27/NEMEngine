#pragma once

//============================================================================
//	include
//============================================================================
#include "EditorTemplateTypes.h"
#include <Engine/Editor/Utility/EditorShell.h>
#include <Engine/Core/Scripting/Managed/ManagedProcessRunner.h>

// json
#include <json.hpp>

namespace Engine {

	//============================================================================
//	EditorDistributionSession class
//	開発者用の配布情報と公開処理を管理する
//============================================================================
	class EditorDistributionSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 配布管理を開く
		void Open();
		// サンプルの公開停止と削除を確認する
		void RequestDeleteSample(const EditorTemplateEntry& entry);
		// 管理画面と処理結果を表示する
		void Draw();
		bool IsBusy() const { return process_.IsRunning(); }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ManagedProcessRunner process_;
		EditorShell::PathSelectionDialog archiveDialog_;
		std::vector<EditorTemplateEntry> samples_;
		std::vector<nlohmann::json> assets_;
		EditorTemplateEntry sample_;
		EditorTemplateEntry deleteSample_;
		nlohmann::json asset_;
		nlohmann::json operation_;
		std::filesystem::path requestPath_;
		std::string message_;
		std::string output_;
		bool open_ = false;
		bool confirmDelete_ = false;
		bool failed_ = false;
		bool deleteAfterWithdraw_ = false;

		//--------- functions ----------------------------------------------------

		// ローカルの配布元一覧を更新する
		void Refresh();
		// ZIPの登録情報を初期化する
		void NewAsset();
		// ZIPと登録情報を配布元へ保存する
		bool SaveAsset();
		// 配布処理を非同期に起動する
		void Start(nlohmann::json request, bool removeSample = false);
		// 公開処理とファイル選択の完了を取り込む
		void Update();
		// サンプルの設定と公開操作を表示する
		void DrawSamples();
		// アセットの設定と公開操作を表示する
		void DrawAssets();
		// 削除の確認を表示する
		void DrawDeletePopup();
	};
}
