#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/IEditorTool.h>

// c++
#include <functional>

namespace Engine {

	//============================================================================
	//	LambdaEditorTool class
	//	Game側から小さなツールを追加しやすくするための汎用ツール
	//============================================================================
	class LambdaEditorTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		using DrawFunc = std::function<void(const EditorToolContext&)>;
		using TickFunc = std::function<void(ToolContext&)>;
		using EnabledFunc = std::function<bool(const ToolContext&)>;

		// 各処理を関数オブジェクトで受け取る
		LambdaEditorTool(ToolDescriptor descriptor, DrawFunc drawFunc,
			TickFunc tickFunc = {}, EnabledFunc enabledFunc = {});

		// 毎フレーム更新
		void Tick(ToolContext& context) override;
		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// 独立したエディタウィンドウを描画する
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		// ツール情報の取得
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
		// 現在の状態で使用可能か
		bool IsEnabled(const ToolContext& context) const override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{};
		DrawFunc drawFunc_{};
		TickFunc tickFunc_{};
		EnabledFunc enabledFunc_{};

		// ウィンドウ表示状態
		bool openWindow_ = false;
	};
} // Engine
