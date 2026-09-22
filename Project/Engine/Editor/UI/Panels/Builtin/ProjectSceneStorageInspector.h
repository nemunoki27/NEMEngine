#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>

namespace Engine {

	//============================================================================
	//	ProjectSceneStorageInspector class
	//	シーン保存の診断結果と修復操作の表示を管理する
	//============================================================================
	class ProjectSceneStorageInspector {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// シーンの検証結果と明示的な修復操作を表示する
		void DrawSceneStoragePopup(const EditorPanelContext& context, AssetDatabase& database);
		// 操作失敗の診断を表示する
		void ReportFailure(const std::string& message);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// ファイル変更時と明示操作時だけシーンの整合性を検査する
		uint64_t sceneStorageRevision_ = 0;
		std::vector<SceneStorageIssue> sceneStorageIssues_;
		std::vector<std::filesystem::path> sceneRecoveries_;
		std::vector<std::string> sceneRecoveryLabels_;
		std::string sceneStorageMessage_;
		std::string actorRestorePath_;
		int selectedSceneIssue_ = -1;
		bool confirmActorRemoval_ = false;
		std::vector<std::filesystem::path> sceneRemovalPreview_;
		bool requestSceneStoragePopup_ = false;
	};
}
