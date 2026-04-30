#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/Editor/Tools/IEditorTool.h>
#include <Engine/Editor/EditorState.h>

namespace Engine {

	//============================================================================
	//	SceneViewCameraController class
	//	シーンビュー内のカメラを制御するクラス
	//============================================================================
	class SceneViewCameraController :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SceneViewCameraController();
		~SceneViewCameraController();

		// カメラの状態を更新する
		void Update(Dimension dimension);

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// CameraManagerウィンドウを描画する
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		// ツール情報を取得する
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }

		ManualRenderCameraState& GetCameraState() { return cameraState_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// ToolPanelへ登録する情報
		ToolDescriptor descriptor_{
			.id = "engine.sceneViewCamera",
			.name = "SceneViewCamera",
			.category = "Camera",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 0,
		};

		// ウィンドウ表示状態
		bool openWindow_ = false;

		// カメラの状態
		ManualRenderCameraState cameraState_;

		//--------- functions ----------------------------------------------------

		// カメラの状態を初期化する
		void MakeDefaultState();

		// カメラの状態を更新できるか
		bool CanUpdate();

		// 2D/3Dカメラの状態を更新する
		void Update3D();
		void Update2D();
	};
} // Engine