#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/IEditorTool.h>

namespace Engine {

	//============================================================================
	//	CameraManagerTool class
	//	ゲーム用カメラの作成と初期設定を行うツール
	//============================================================================
	class CameraManagerTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		CameraManagerTool() = default;
		~CameraManagerTool() override = default;

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// CameraManagerウィンドウを描画する
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		// ツール情報を取得する
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// ToolPanelへ登録する情報
		ToolDescriptor descriptor_{
			.id = "engine.camera_manager",
			.name = "CameraManager",
			.category = "Camera",
			.description = "Create and setup gameplay cameras.",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 40,
		};

		// ウィンドウ表示状態
		bool openWindow_ = false;

		//--------- functions ----------------------------------------------------

		// CameraManagerウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);
		// 選択中Entityをターゲットとして取得する
		UUID GetSelectedTargetUUID(const EditorToolContext& context) const;
		// 2D追従カメラを作成する
		void Create2DFollowCamera(const EditorToolContext& context);
		// 3D追従カメラを作成する
		void Create3DFollowCamera(const EditorToolContext& context);
		// 3D注視カメラを作成する
		void Create3DLookAtCamera(const EditorToolContext& context);
		// 選択中EntityへCameraControllerを追加する
		void AddControllerToSelected(const EditorToolContext& context);
		// 選択中カメラをSceneViewに割り当てる
		void AssignSelectedCameraToSceneView(const EditorToolContext& context);
	};
} // Engine
