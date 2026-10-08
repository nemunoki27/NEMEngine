#pragma once

//============================================================================
//	include
//============================================================================
#include "SceneViewCameraSettings.h"
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/Platform/Input/InputTypes.h>

namespace Engine {

	//============================================================================
	//	SceneViewCameraController class
	//	シーンビュー内のカメラを制御するクラス
	//============================================================================
	class SceneViewCameraController : public IEditorTool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit SceneViewCameraController(bool persistSettings = true);
		~SceneViewCameraController();

		// カメラの状態を初期化する
		void MakeDefaultState();
		// 保存した位置と操作速度を読み込む
		void MakeFromJson(const std::string& filePath);

		// カメラの状態を更新する
		void Update(Dimension dimension, InputViewArea viewArea);

		// 指定座標へ滑らかに寄る
		void FocusOn(const Vector3& worldPosition);
		// 入力対象でない間もフォーカスを進める
		void UpdateFocus();
		// フォーカスで寄っている最中か
		bool IsFocusing() const { return focusActive_; }

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// CameraManagerウィンドウを描画する
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		// 次回終了時の設定保存先を指定する
		void SetSavePath(const std::string& savePath);

		// ツール情報を取得する
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }

		// 手動描画に使うカメラ状態を取得する
		ManualRenderCameraState& GetCameraState() { return settings_.cameraState; }

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
		SceneViewCameraSettings settings_;

		// フォーカス中か
		bool focusActive_ = false;
		// 寄り先のカメラ位置
		Vector3 focusTargetPos_ = Vector3::AnyInit(0.0f);

		//--------- functions ----------------------------------------------------

		// カメラの状態を更新できるか
		bool CanUpdate(InputViewArea viewArea);

		// 3Dカメラの位置と回転を更新する
		void Update3D();
		// 2Dカメラの位置とズームを更新する
		void Update2D(InputViewArea viewArea);
	};
} // Engine
