#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/Editor/EditorState.h>

namespace Engine {

	//============================================================================
	//	SceneViewCameraController class
	//	シーンビュー内のカメラを制御するクラス
	//============================================================================
	class SceneViewCameraController {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SceneViewCameraController() = default;
		~SceneViewCameraController();

		// カメラの状態を初期化する
		void MakeDefaultState();

		// カメラの状態を更新する
		void Update(Dimension dimension);

		//--------- accessor -----------------------------------------------------

		ManualRenderCameraState& GetCameraState() { return cameraState_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// カメラの状態
		ManualRenderCameraState cameraState_;

		//--------- functions ----------------------------------------------------

		// カメラの状態を更新できるか
		bool CanUpdate();

		// 2D/3Dカメラの状態を更新する
		void Update3D();
		void Update2D();
	};
} // Engine