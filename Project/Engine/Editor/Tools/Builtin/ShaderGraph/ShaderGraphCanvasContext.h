#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

namespace ax::NodeEditor {

	struct EditorContext;
}

namespace Engine {

	//============================================================================
	//	ShaderGraphCanvasContext class
	//	Node Editorの寿命と位置復元と選択を管理する
	//============================================================================
	class ShaderGraphCanvasContext {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphCanvasContext() = default;
		~ShaderGraphCanvasContext();
		ShaderGraphCanvasContext(const ShaderGraphCanvasContext&) = delete;
		ShaderGraphCanvasContext& operator=(const ShaderGraphCanvasContext&) = delete;

		// 必要なときだけCanvasを作成する
		void EnsureCreated();
		// Canvasを破棄して位置復元を予約する
		void Reset();
		// 保存前に表示位置をGraphへ取り込む
		void CapturePositions(ShaderGraphAsset& graph);
		// Graphの位置とGroupサイズを復元する
		void RestorePositions(const ShaderGraphAsset& graph);
		// 診断対象のNodeへ移動する
		void NavigateToNode(UUID nodeID);

		//--------- accessor -----------------------------------------------------

		ax::NodeEditor::EditorContext* GetEditor() const { return nodeEditor_; }
		void RequestPositionRestore() { restoreNodePositions_ = true; }
		// 選択対象からGraphに存在するNodeだけを取得する
		std::vector<UUID> GetSelectedNodes(const ShaderGraphAsset& graph) const;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 所有するNode Editor
		ax::NodeEditor::EditorContext* nodeEditor_ = nullptr;
		// 読込位置の復元予約
		bool restoreNodePositions_ = false;
	};
} // Engine
