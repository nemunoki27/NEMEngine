#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphNodeCreation.h"

// c++
#include <string>

namespace Engine {

	class ShaderGraphCanvasContext;
	class ShaderGraphGroupEditor;
	class ShaderGraphNodeTransfer;

	//============================================================================
	//	ShaderGraphCanvasMenu class
	//	Canvasの右クリック操作とNode検索を保持する
	//============================================================================
	class ShaderGraphCanvasMenu {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphCanvasMenu(ShaderGraphEditSession& session, ShaderGraphCanvasContext& canvas,
			ShaderGraphGroupEditor& groupEditor, ShaderGraphNodeTransfer& nodeTransfer);

		// Nodeの操作popupを開く
		void OpenNode(UUID nodeID);
		// 指定位置へ追加するNodeを選ぶ
		void OpenCreate(Vector2 position);
		// 開いているpopupを表示する
		void Draw();
		// Graph切替時に操作対象を解除する
		void Reset();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ShaderGraphEditSession& editSession_;
		ShaderGraphCanvasContext& canvas_;
		ShaderGraphGroupEditor& groupEditor_;
		ShaderGraphNodeTransfer& nodeTransfer_;
		ShaderGraphNodeCreation nodeCreation_;
		std::string nodeSearch_{};
		Vector2 createNodePosition_{};
		UUID contextNode_{};

		//--------- functions ----------------------------------------------------

		// Node追加の選択画面を表示する
		void DrawNodeCreationMenu();
		// Nodeを複製して配置する
		void DuplicateNode(UUID nodeID);
		// 種類に応じたNodeを配置する
		void AddNode(ShaderGraphNodeKind kind, Vector2 position);
		// 定数Nodeを配置する
		void AddConstantNode(ShaderGraphValueType type, Vector2 position);
		// Parameter参照を配置する
		void AddParameterNode(UUID parameterID, Vector2 position);
		// Keyword参照を配置する
		void AddKeywordNode(UUID keywordID, Vector2 position);
		// 作成したNodeをCanvasへ配置する
		void PlaceCreatedNode(UUID nodeID, Vector2 position);
	};
} // Engine
