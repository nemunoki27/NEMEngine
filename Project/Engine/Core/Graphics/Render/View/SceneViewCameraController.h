#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/Editor/Tools/Interface/IEditorTool.h>
#include <Engine/Editor/EditorState.h>
#include <Engine/Core/Input/InputStructures.h>

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

		// カメラの状態を初期化する
		void MakeDefaultState();
		void MakeFromJson(const std::string& filePath);

		// カメラの状態を更新する
		void Update(Dimension dimension, InputViewArea viewArea);

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// CameraManagerウィンドウを描画する
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		void SetSavePath(const std::string& savePath) { savePath_ = savePath; }

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

		// 閉じた時に保存するカメラパス
		std::string savePath_ = "";

		// カメラの状態
		ManualRenderCameraState cameraState_;

		//--------- functions ----------------------------------------------------

		// カメラの状態を更新できるか
		bool CanUpdate(InputViewArea viewArea);

		// 2D/3Dカメラの状態を更新する
		void Update3D();
		void Update2D();
	};
} // Engine