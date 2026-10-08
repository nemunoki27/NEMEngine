#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include "MaterialCreationSession.h"
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <string>

// directX
#include <d3d12.h>

namespace Engine {

	//============================================================================
	//	MaterialEditorTool class
	//	描画タイプごとのデフォルトマテリアル設定とマテリアル/パイプラインの作成を行うツール
	//============================================================================
	class MaterialEditorTool :
		public IEditorTool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		MaterialEditorTool() = default;
		~MaterialEditorTool() override = default;

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// Materialツールウィンドウを描画する
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
			.id = "engine.material_editor",
			.name = "マテリアル作成",
			.category = "レンダリング",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 1,
		};

		// ウィンドウ表示状態
		bool openWindow_ = false;

		MaterialCreationSession creationSession_;
		bool typeDefaultsInitialized_ = false;

		//--------- functions ----------------------------------------------------

		// Materialツールウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);
		// 描画タイプごとのデフォルトマテリアル設定セクションを描画する
		void DrawDefaultMaterialSection(const EditorToolContext& context);
		// マテリアル/パイプライン作成セクションを描画する
		void DrawCreateMaterialSection(const EditorToolContext& context);
	};
} // Engine
