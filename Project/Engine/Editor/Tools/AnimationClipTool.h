#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Interface/IEditorTool.h>

namespace Engine {

	//============================================================================
	//	AnimationClipTool class
	//	アニメーションの動きを作成するツール
	//============================================================================
	class AnimationClipTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AnimationClipTool() = default;
		~AnimationClipTool() = default;

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// AnimationCurveウィンドウを描画する
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		// ツール情報を取得する
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{
			.id = "engine.animation_clip",
			.name = "AnimationClip",
			.category = "Animation",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 2,
		};

		bool openWindow_ = false;

		// プレビュー用レンダーターゲットの設定
		const Vector2I kPreviewSize_ = Vector2I(384, 216);
		const Color4 kPreviewColor_ = Color4::FromHex(0x202f55ff);
		const uint32_t kPreviewColorTargetCount_ = 3;

		//--------- functions ----------------------------------------------------

		// AnimationClipウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);
		// RenderTextureの表示テストを描画する
		void DrawPreview(const EditorToolContext& context);
		// RenderTextureへプレビューEntityを描画する
		void RenderPreviewEntity(const EditorToolContext& context, EditorToolRenderTexture& preview);
		// プレビュー対象Entityの表示名を取得する
		std::string GetPreviewEntityName(const EditorToolContext& context,
			const EditorToolRenderTexture& preview) const;
	};
} // Engine
