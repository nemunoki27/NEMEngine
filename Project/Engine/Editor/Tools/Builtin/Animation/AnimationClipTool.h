#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include "AnimationClipEditSession.h"

// c++
#include <optional>

namespace Engine {

	//============================================================================
	//	AnimationClipTool class
	//	アニメーションの動きを作成するツール
	//============================================================================
	class AnimationClipTool : public IEditorTool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		AnimationClipTool() = default;
		~AnimationClipTool() override = default;

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// AnimationCurveウィンドウを描画する
		void DrawEditorTool(const EditorToolContext& context) override;
		// 未保存の編集があるか判定する
		bool HasPendingEdits() const override;
		// 終了前に未保存編集の確認を要求する
		void RequestResolvePendingEdits() override;
		// 未保存編集の確認結果を取り出す
		EditorToolCloseResult ConsumePendingEditCloseResult() override;
		// Sceneのプレビュー値を元へ戻す
		void EndScenePreview() override;

		//--------- accessor -----------------------------------------------------

		// ツール情報を取得する
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// Tool一覧へ登録する情報
		ToolDescriptor descriptor_{
			.id = "engine.animation_clip",
			.name = "アニメクリップ作成",
			.category = "アニメーション",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 2,
		};

		// ウィンドウの表示状態
		bool openWindow_ = false;
		// 未保存確認後の切替先
		std::optional<AssetID> pendingClip_;
		// 終了要求と確認結果
		bool pendingClose_ = false;
		bool resolvingClose_ = false;
		EditorToolCloseResult closeResult_ = EditorToolCloseResult::None;

		// Clipの編集とプレビューのセッション
		AnimationClipEditSession session_;

		//--------- functions ----------------------------------------------------

		// アセット、編集設定UI
		void DrawToolbarUI(const EditorToolContext& context);
		// 未保存編集を確認してClipを切り替える
		void RequestClipSwitch(const EditorToolContext& context, AssetID assetID);
		// 保存と破棄の確認を表示する
		void DrawPendingEdits(const EditorToolContext& context);
		// Clipの選択と保存を表示する
		void DrawClipAssetUI(const EditorToolContext& context);
		// 編集対象と再生操作を表示する
		void DrawEditAssetUI(const EditorToolContext& context);
	};
} // Engine
