#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphAssetAuthoring.h"

namespace Engine {

	class ShaderGraphEditSession;
	struct EditorToolContext;

	//============================================================================
	//	IShaderGraphToolbarActions class
	//	Toolbarの入力をToolの操作へ渡す
	//============================================================================
	class IShaderGraphToolbarActions {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		virtual ~IShaderGraphToolbarActions() = default;

		// Graphの切替を要求する
		virtual void RequestGraphSwitch(const EditorToolContext& context, AssetID assetID) = 0;
		// 別Assetの設定を取り込む
		virtual void ImportGraphSettings(const EditorToolContext& context, AssetID source) = 0;
		// 未保存状態を確認して作成する
		virtual void RequestGraphCreation(const EditorToolContext& context) = 0;
		// 表示位置を確定して保存する
		virtual bool SaveGraph(const EditorToolContext& context) = 0;
		// 保存してShaderをコンパイルする
		virtual bool SaveAndCompile(const EditorToolContext& context) = 0;
		// 直前のGraph編集へ戻す
		virtual void UndoGraph() = 0;
		// 戻したGraph編集を再適用する
		virtual void RedoGraph() = 0;
	};

	//============================================================================
	//	ShaderGraphToolbar class
	//	Asset選択と作成設定と保存履歴の操作を表示する
	//============================================================================
	class ShaderGraphToolbar {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 入力が成立した位置でToolの操作を呼び出す
		void Draw(const EditorToolContext& context, ShaderGraphEditSession& session, IShaderGraphToolbarActions& actions);

		//--------- accessor -----------------------------------------------------

		const ShaderGraphCreationRequest& GetCreationRequest() const { return creation_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 新規Graphの作成入力
		ShaderGraphCreationRequest creation_;
	};
} // Engine
