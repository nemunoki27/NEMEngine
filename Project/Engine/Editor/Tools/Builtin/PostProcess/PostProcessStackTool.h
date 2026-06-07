#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <cstdint>
#include <string>

namespace Engine {

	//============================================================================
	//	PostProcessStackTool class
	//	PostProcessStackを編集するエディタツール
	//============================================================================
	class PostProcessStackTool :
		public IEditorTool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		PostProcessStackTool() = default;
		~PostProcessStackTool() override = default;

		// ToolPanel外で毎フレーム必要な処理を行う
		void Tick(ToolContext& context) override;
		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// PostProcessStackウィンドウを描画する
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		// ツール情報を取得する
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// ToolPanelへ登録する情報
		ToolDescriptor descriptor_{
			.id = "engine.postprocess_stack",
			.name = "PostProcessStack",
			.category = "Rendering",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::AllowPlayMode,
			.order = 0,
		};

		// ウィンドウ表示状態
		bool openWindow_ = false;
		// 選択中のパスインデックス (-1: 未選択)
		int32_t selectedPassIndex_ = -1;
		// 最後に確認したPostProcessStackアセット
		AssetID lastStackAsset_{};

		// 未保存確認ポップアップ用
		bool pendingScenePathChange_ = false;
		AssetID pendingNextStackAsset_{};

		//--------- functions ----------------------------------------------------

		// PostProcessStackウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);
		// パス一覧を描画する
		void DrawPassList();
		// 選択中パスの詳細を描画する
		void DrawPassDetail(const EditorToolContext& context);
		// ドロップゾーン（スタックファイル/マテリアル追加）を描画する
		void DrawDropZones(const EditorToolContext& context);
		// 未保存確認ポップアップを描画する
		void DrawUnsavedConfirmPopup(const EditorToolContext& context);
	};
} // Engine

