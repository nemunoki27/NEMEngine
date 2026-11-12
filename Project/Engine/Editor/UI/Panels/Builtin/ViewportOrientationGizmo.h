#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Camera/SceneViewCameraNavigation.h>
#include <optional>
#include <imgui.h>

namespace Engine {

	struct EditorPanelContext;
	struct ViewportOrientationLayout {

		ImVec2 position{};
		float size = 0.0f;
	};

	//============================================================================
	//	ViewportOrientationGizmo class
	//	シーンカメラの軸表示と周回入力を管理する
	//============================================================================
	class ViewportOrientationGizmo {
	public:
		// 表示と操作ボタンで軸色を共有する
		static ImU32 GetAxisColor(int axis);
		// 画像内の配置を描画と入力判定で共有する
		static ViewportOrientationLayout GetLayout(const EditorPanelContext& context, const ImVec2& position, const ImVec2& size);
		// 軸の向きと操作領域を表示する
		std::optional<SceneViewCameraGizmoCommand> Draw(const EditorPanelContext& context,
			const ViewportOrientationLayout& layout, bool allowInteraction = true);
		// 操作中の入力を他の編集へ渡さない
		bool IsActive() const { return pressed_; }
		void Reset() { pressed_ = false; dragged_ = false; pressedAxis_ = -1; }
	private:
		bool pressed_ = false;
		bool dragged_ = false;
		int pressedAxis_ = -1;
		ImVec2 pressMouse_{}, lastMouse_{};
	};
}
