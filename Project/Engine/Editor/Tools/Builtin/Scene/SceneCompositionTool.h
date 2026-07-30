#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>

// c++
#include <string>

namespace Engine {

	// front
	struct SceneInstance;

	//============================================================================
	//	SceneCompositionTool class
	//	アクティブシーンのSubScene構成を編集するツール
	//============================================================================
	class SceneCompositionTool :
		public IEditorTool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SceneCompositionTool() = default;
		~SceneCompositionTool() override = default;

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// シーン構成ウィンドウを描画
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{
			.id = "engine.scene_composition",
			.name = "シーン構成",
			.category = "シーン",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 0,
		};

		bool openWindow_ = false;
		std::string statusMessage_;
		bool statusError_ = false;

		//--------- functions ----------------------------------------------------

		// シーン構成ウィンドウを描画
		void DrawWindow(const EditorToolContext& context);
		// 変更を検証してロード中SubSceneへ反映
		bool ApplyChanges(const EditorToolContext& context,
			SceneInstance& instance, const std::vector<SubSceneSlotDesc>& previous);
	};
} // Engine
