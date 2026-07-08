#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>

namespace Engine {

	//============================================================================
	//	CollisionManagerTool class
	//	Collisionタイプと衝突マトリクスを編集するツール
	//============================================================================
	class CollisionManagerTool :
		public IEditorTool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		CollisionManagerTool() = default;
		~CollisionManagerTool() override = default;

		// ToolPanel外で毎フレーム必要な処理を行う
		void Tick(ToolContext& context) override;
		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// CollisionManagerウィンドウを描画する
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
			.id = "engine.collision_manager",
			.name = "衝突設定",
			.category = "物理",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::AllowPlayMode,
			.order = 0,
		};

		// ウィンドウ表示状態
		bool openWindow_ = false;
		// 設定に未保存の編集があるか
		bool dirty_ = false;
		// 削除コンボで選択中のCollisionタイプindex
		int32_t removeTypeIndex_ = 0;

		//--------- functions ----------------------------------------------------

		// CollisionManagerウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);
		// Collisionタイプ一覧を描画する、変更があればtrue
		bool DrawTypes();
		// Collisionタイプ同士の衝突マトリクスを描画する、変更があればtrue
		bool DrawMatrix();
		// World内のCollision形状をLineRendererで描画する
		void DrawCollisionWorld(ECSWorld& world) const;
	};
} // Engine

