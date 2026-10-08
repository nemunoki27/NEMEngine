#pragma once

//============================================================================
//	include
//============================================================================
#include <vector>

namespace Engine {

	class ShaderGraphEditSession;
	class ShaderGraphCanvasContext;
	class ShaderGraphAppearanceEditor;
	class ShaderGraphNodePreviews;
	class ShaderGraphNodeDrawer;
	class ShaderGraphGroupEditor;
	class ShaderGraphCanvasMenu;
	struct EditorToolContext;

	// Canvasから保存と履歴へ渡す操作要求
	enum class ShaderGraphCanvasCommand {

		Copy,
		Paste,
		Duplicate,
		Save,
		Undo,
		Redo,
	};

	//============================================================================
	//	ShaderGraphCanvasView class
	//	Graphの表示と接続操作を保存処理から分離する
	//============================================================================
	class ShaderGraphCanvasView {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphCanvasView(ShaderGraphEditSession& editSession, ShaderGraphCanvasContext& canvas,
			ShaderGraphAppearanceEditor& appearanceEditor, ShaderGraphNodePreviews& nodePreviews,
			ShaderGraphNodeDrawer& nodeDrawer, ShaderGraphGroupEditor& groupEditor, ShaderGraphCanvasMenu& canvasMenu);

		// Canvasを表示して入力順の操作要求を返す
		std::vector<ShaderGraphCanvasCommand> Draw(const EditorToolContext& context, bool commandPanelFocused);
		// 診断を表示して選択したNodeへ移動する
		void DrawDiagnostics();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// Toolが所有する編集状態と表示処理
		ShaderGraphEditSession& editSession_;
		ShaderGraphCanvasContext& canvas_;
		ShaderGraphAppearanceEditor& appearanceEditor_;
		ShaderGraphNodePreviews& nodePreviews_;
		ShaderGraphNodeDrawer& nodeDrawer_;
		ShaderGraphGroupEditor& groupEditor_;
		ShaderGraphCanvasMenu& canvasMenu_;
	};
} // Engine
