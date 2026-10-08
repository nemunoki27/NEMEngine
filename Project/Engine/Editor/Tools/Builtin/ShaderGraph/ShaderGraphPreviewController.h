#pragma once

namespace Engine {

	class ShaderGraphEditSession;
	class ShaderGraphScenePreview;
	struct EditorToolContext;

	//============================================================================
	//	ShaderGraphPreviewController class
	//	Sceneプレビューの対象選択と自動コンパイルを管理する
	//============================================================================
	class ShaderGraphPreviewController {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphPreviewController(ShaderGraphEditSession& session, ShaderGraphScenePreview& preview);

		// Scene上のプレビュー対象を選択する
		void DrawSettings(const EditorToolContext& context);
		// 入力の確定を待って未保存Graphをコンパイルする
		void Update(const EditorToolContext& context);
		// 適用前のMaterialとコンパイル待機を復元する
		void Restore();
		// 復元済みの対象とコンパイル待機を解除する
		void ResetTarget();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ShaderGraphEditSession& editSession_;
		ShaderGraphScenePreview& scenePreview_;
		// 入力確定後のコンパイル予定時刻
		double compileDeadline_ = 0.0;

		//--------- functions ----------------------------------------------------

		// コンパイル済みMaterialをSceneへ反映する
		bool Apply(const EditorToolContext& context);
	};
} // Engine
