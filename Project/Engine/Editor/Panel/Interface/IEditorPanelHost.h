#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/Interface/IEditorCommand.h>
#include <Engine/Core/Asset/AssetTypes.h>

// c++
#include <memory>

namespace Engine {

	//============================================================================
	//	IEditorPanelHost class
	//	パネルから操作を依頼するための窓口
	//============================================================================
	class IEditorPanelHost {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

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

		// プレイ/ストップの切り替え要求
		virtual void RequestPlayToggle() = 0;
		// 新規シーン作成要求
		virtual void RequestNewScene() = 0;
		// シーンを開く要求
		virtual void RequestOpenScene(AssetID sceneAsset) = 0;
		// アクティブシーンの保存要求
		virtual void RequestSaveScene() = 0;
	};
} // Engine
