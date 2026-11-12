#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>

// c++
#include <array>
#include <cstdint>
#include <optional>

namespace Engine {

	struct SceneViewCameraSettings;

	enum class SceneViewCameraGizmoAction { Align, Orbit };
	struct SceneViewCameraGizmoCommand {

		SceneViewCameraGizmoAction action = SceneViewCameraGizmoAction::Align;
		Vector3 axis{};
		Vector2 delta{};
	};

	// シーンカメラ全体の操作方式
	enum class SceneViewCameraControlStyle { UE, Unity, Blender, Maya, Original };
	// 個別の操作に使う方式
	enum class SceneViewCameraControlSource { Disabled, UE, Unity, Blender, Maya };
	// 個別に割り当てるカメラ操作
	enum class SceneViewCameraAction { Move, Pan, Look, Orbit, Zoom };
	constexpr size_t kSceneViewCameraActionCount = 5;
	using SceneViewCameraControls = std::array<SceneViewCameraControlSource, kSceneViewCameraActionCount>;

	// 入力機器に依存しないカメラ操作の状態
	struct SceneViewCameraInput {

		static constexpr uint8_t kLeft = 1;
		static constexpr uint8_t kRight = 2;
		static constexpr uint8_t kMiddle = 4;
		static constexpr uint8_t kShift = 1;
		static constexpr uint8_t kControl = 2;
		static constexpr uint8_t kAlt = 4;

		bool available = false;
		uint8_t buttons = 0;
		uint8_t pressedButtons = 0;
		uint8_t modifiers = 0;
		Vector2 mouseDelta{};
		float wheel = 0.0f;
		Vector3 movement{};
		float deltaTime = 0.0f;
	};

	// 解決済みの操作をカメラ計算へ渡す
	struct SceneViewCameraNavigationCommand {

		bool active = false;
		bool pan = false;
		bool look = false;
		bool orbit = false;
		bool zoomDrag = false;
		bool accelerated = false;
		bool preview = false;
		Vector3 movement{};
		float zoomWheel = 0.0f;
		float speedWheel = 0.0f;
	};

	// 周回中心を描画用カメラから独立して保持する
	struct SceneViewCameraNavigationState {

		Vector3 pivot{};
		bool pivotValid = false;
		bool aligning = false;
		Quaternion alignmentStart{}, alignmentEnd{};
		float alignmentElapsed = 0.0f;
		float alignmentDistance = 0.0f;
		Quaternion rotation{};
		Vector3 rotationEuler{}, alignmentAxis{};
		bool rotationValid = false;
	};

	// 重なった操作を設定画面へ返す
	struct SceneViewCameraControlConflict {

		SceneViewCameraAction first = SceneViewCameraAction::Move;
		SceneViewCameraAction second = SceneViewCameraAction::Pan;
	};

	namespace SceneViewCameraNavigationUtility {

		// 選択中の方式から個別割当を取り出す
		SceneViewCameraControls ResolveControls(SceneViewCameraControlStyle style, const SceneViewCameraControls& custom);
		// 同じ入力を異なる操作へ割り当てていないか調べる
		std::optional<SceneViewCameraControlConflict> FindConflict(const SceneViewCameraControls& controls);
		// 選択した方式に対象の操作があるか調べる
		bool SupportsAction(SceneViewCameraAction action, SceneViewCameraControlSource source);
		// 操作名を設定画面へ返す
		const char* GetActionLabel(SceneViewCameraAction action);
		// 操作方法を設定画面へ返す
		const char* GetControlDescription(SceneViewCameraAction action, SceneViewCameraControlSource source);
		// 解決済みの入力で位置と回転と速度を更新する
		void ApplyMotion(SceneViewCameraSettings& settings, SceneViewCameraNavigationState& state,
			const SceneViewCameraNavigationCommand& command, const SceneViewCameraInput& input);
		// 軸整列と周回の要求を手動カメラへ反映する
		void ApplyGizmo(SceneViewCameraSettings& settings, SceneViewCameraNavigationState& state,
			const SceneViewCameraGizmoCommand& command);
		// 注視点を維持して軸整列を進める
		void UpdateAlignment(SceneViewCameraSettings& settings, SceneViewCameraNavigationState& state, float deltaTime);
		// 周回中心と距離を用意する
		float PreparePivot(SceneViewCameraSettings& settings, SceneViewCameraNavigationState& state);
		// 設定画面の変更を取り込み回転を取得する
		Quaternion GetRotation(const SceneViewCameraSettings& settings, SceneViewCameraNavigationState& state);
		// 姿勢を保持して描画と設定画面へ回転を渡す
		void SetRotation(SceneViewCameraSettings& settings, SceneViewCameraNavigationState& state, const Quaternion& rotation);
		// 自由視点と周回で同じ回転を適用する
		void Rotate(SceneViewCameraSettings& settings, SceneViewCameraNavigationState& state, const Vector2& delta, bool orbit);
	}

	//============================================================================
	//	SceneViewCameraNavigationSession class
	//	ビュー内で開始したカメラ操作の入力を保持する
	//============================================================================
	class SceneViewCameraNavigationSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 操作方式に従って現在の入力を解決する
		SceneViewCameraNavigationCommand Resolve(const SceneViewCameraControls& controls, const SceneViewCameraInput& input);
		// フォーカス喪失や方式切替で操作を終了する
		void Cancel();

		//--------- accessor -----------------------------------------------------

		bool IsActive() const { return active_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		uint8_t capturedButtons_ = 0;
		bool ownsGesture_ = false;
		bool active_ = false;
	};
}
