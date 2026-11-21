#pragma once

//============================================================================
//	include
//============================================================================
#include "EditorDeveloperPipe.h"
#include "EditorTemplateTypes.h"

// c++
#include <future>

namespace Engine {

	struct EditorPanelContext;

	//============================================================================
	//	EditorDeveloperLauncher class
	//	開発者ランチャーと編集シーンを接続する
	//============================================================================
	class EditorDeveloperLauncher {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		~EditorDeveloperLauncher();

		// バージョン管理を開くボタンを表示する
		void DrawButton();
		// 編集シーンに関する要求を処理する
		void Update(const EditorPanelContext& context);

	private:
		//--------- variables ----------------------------------------------------

		EditorDeveloperPipe pipe_;
		std::optional<bool> available_;
		std::optional<EditorTemplateSaveRequest> request_;
		std::future<EditorTemplateResult> staging_;
		UUID sourceInstance_{};
		bool interrupted_ = false;
		HANDLE bootstrap_ = nullptr;

		//--------- functions ----------------------------------------------------

		void Launch();
		void Dispatch(const nlohmann::json& data, const EditorPanelContext& context);
		void BeginSave(const nlohmann::json& data, const EditorPanelContext& context);
		void UpdateSave(const EditorPanelContext& context);
	};
} // Engine
