#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Editor/Core/Layout/EditorLayoutTypes.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <memory>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	IEditorPanelHost class
	//	パネルから操作を依頼するための窓口
	//============================================================================
	class IEditorPanelHost {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		IEditorPanelHost() = default;
		virtual ~IEditorPanelHost() = default;

		// エディタコマンド実行
		virtual bool ExecuteEditorCommand(std::unique_ptr<IEditorCommand> command) = 0;

		// コマンド履歴操作
		virtual bool UndoEditorCommand() = 0;
		virtual bool RedoEditorCommand() = 0;

		// 編集操作
		virtual bool DuplicateSelection() = 0;
		virtual bool CopySelectionToClipboard() = 0;
		virtual bool PasteClipboard() = 0;

		// パネル複製要求
		virtual void RequestDuplicatePanel(const std::string& instanceID) = 0;
		// エディターレイアウト一覧を取得
		virtual const std::vector<EditorLayoutMenuEntry>& GetEditorLayoutEntries() const = 0;
		virtual const std::string& GetActiveEditorLayoutID() const = 0;
		virtual bool IsEngineLayoutSaveAvailable() const = 0;
		// エディターレイアウト操作
		virtual bool RequestSaveEditorLayout(const std::string& name, std::string& outError) = 0;
		virtual void RequestSaveAllEngineLayouts() = 0;
		virtual void RequestApplyEditorLayout(const std::string& layoutID) = 0;
		virtual void RequestDeleteEditorLayout(const std::string& layoutID) = 0;
		virtual void RequestImportEditorLayouts() = 0;

		// プレイ/ストップの切り替え要求
		virtual void RequestPlayToggle() = 0;
		// 一時停止中のPlay再開要求
		virtual void RequestPlayResume() = 0;
		// Play中の一時停止要求
		virtual void RequestPlayPause() = 0;
		// Play一時停止中の1フレーム送り要求
		virtual void RequestPlayFrameStep() = 0;
		// 新規シーン作成要求
		virtual void RequestNewScene() = 0;
		// シーンを開く要求
		virtual void RequestOpenScene(AssetID sceneAsset) = 0;
		// アクティブシーンの保存要求
		virtual void RequestSaveScene() = 0;
		// アクティブシーンを未保存状態にする
		virtual void RequestMarkSceneDirty() = 0;
		// プレファブ編集モードへ入る要求、隔離ワールドで編集する
		virtual void RequestEnterPrefabEdit(AssetID prefabAsset) = 0;
		// プレファブ編集モードを抜ける要求、ネスト中は1階層戻る
		virtual void RequestExitPrefabEdit() = 0;
		// プレファブ編集を一括で抜けて元のシーン編集へ戻る要求
		virtual void RequestExitPrefabEditAll() = 0;
		// In-Context編集のオンオフ切り替え要求、オンで元シーンに置いて編集する
		virtual void RequestTogglePrefabInContext() = 0;
		// 編集中プレファブの保存要求
		virtual void RequestSavePrefab() = 0;
	};
} // Engine
