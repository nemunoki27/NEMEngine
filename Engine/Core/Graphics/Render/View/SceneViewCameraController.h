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
		~SceneViewCameraController() = default;

		// カメラの状態を初期化する
		static ManualRenderCameraState MakeDefaultState();

		// カメラの状態を更新する
		static void Update(ManualRenderCameraState& state, Dimension dimension);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// カメラの状態を更新できるか
		static bool CanUpdate();

		// 2D/3Dカメラの状態を更新する
		static void Update3D(ManualRenderCameraState& state);
		static void Update2D(ManualRenderCameraState& state);
	};
} // Engine