#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/IEditorTool.h>

namespace Engine {

	//============================================================================
	//	CollisionManagerTool class
	//	Collisionタイプと衝突マトリクスを編集するツール
	//============================================================================
	class CollisionManagerTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

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
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// ToolPanelへ登録する情報
		ToolDescriptor descriptor_{
			.id = "engine.collision_manager",
			.name = "CollisionManager",
			.category = "Physics",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::AllowPlayMode,
			.order = 0,
		};

		// ウィンドウ表示状態
		bool openWindow_ = false;
		// シーン上のCollision形状を描画するか
		bool drawCollisionWorld_ = false;

		//--------- functions ----------------------------------------------------

		// CollisionManagerウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);
		// Collisionタイプ一覧を描画する
		void DrawTypes();
		// Collisionタイプ同士の衝突マトリクスを描画する
		void DrawMatrix();
		// World内のCollision形状をLineRendererで描画する
		void DrawCollisionWorld(ECSWorld& world) const;
	};
} // Engine
