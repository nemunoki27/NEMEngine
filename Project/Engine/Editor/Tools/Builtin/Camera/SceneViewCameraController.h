#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/Platform/Input/InputTypes.h>

namespace Engine {

	//============================================================================
	//	SceneViewCameraController class
	//	シーンビュー内のカメラを制御するクラス
	//============================================================================
	class SceneViewCameraController :
		public IEditorTool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit SceneViewCameraController(bool persistSettings = true);
		~SceneViewCameraController();

		// カメラの状態を初期化する
		void MakeDefaultState();
		void MakeFromJson(const std::string& filePath);

		// カメラの状態を更新する
		void Update(Dimension dimension, InputViewArea viewArea);

		// 指定ワールド座標へ向けて滑らかに寄る、3Dマニュアルカメラ用
		void FocusOn(const Vector3& worldPosition);
		// フォーカス中の寄りを毎フレーム進める、入力可否に関わらず呼ぶ
		void UpdateFocus();
		// フォーカスで寄っている最中か
		bool IsFocusing() const { return focusActive_; }

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// CameraManagerウィンドウを描画する
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		void SetSavePath(const std::string& savePath);

		// ツール情報を取得する
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }

		ManualRenderCameraState& GetCameraState() { return cameraState_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// ToolPanelへ登録する情報
		ToolDescriptor descriptor_{
			.id = "engine.sceneViewCamera",
			.name = "シーンカメラ設定",
			.category = "カメラ",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 0,
		};

		// ウィンドウ表示状態
		bool openWindow_ = false;

		// 閉じた時に保存するカメラパス
		std::string savePath_ = "";
		bool persistSettings_ = true;

		// カメラの状態
		ManualRenderCameraState cameraState_;

		// フォーカス中か
		bool focusActive_ = false;
		// 寄り先のカメラ位置
		Vector3 focusTargetPos_ = Vector3::AnyInit(0.0f);

		// カメラ操作速度、シリアライズ対象
		float zoomRate2D_ = 0.15f;
		float panSpeed2D_ = 1.0f;
		float rotateSpeed_ = 0.005f;
		float zoomRate_ = 0.4f;
		float panSpeed_ = 0.02f;

		//--------- functions ----------------------------------------------------

		// カメラの状態を更新できるか
		bool CanUpdate(InputViewArea viewArea);

		// 2D/3Dカメラの状態を更新する
		void Update3D();
		void Update2D(InputViewArea viewArea);
	};
} // Engine
