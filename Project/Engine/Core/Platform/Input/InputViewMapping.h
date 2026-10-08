#pragma once

//============================================================================
//	include
//============================================================================
#include "InputDeviceState.h"

// c++
#include <optional>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	InputViewMapping class
	//	描画矩形と入力座標の変換を管理する
	//============================================================================
	class InputViewMapping {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		void SetViewRect(InputViewArea viewArea, const Vector2& dstPos, const Vector2& dstSize, const Vector2& srcSize,
			InputViewCoordinateSpace coordinateSpace);

		bool HasViewRect(InputViewArea viewArea) const;

		bool IsMouseOnView(InputViewArea viewArea, const InputDeviceState& state) const;

		std::optional<Vector2> GetMousePosInView(InputViewArea viewArea, const InputDeviceState& state) const;

		Vector2 GetMouseMoveValueInView(InputViewArea viewArea, const Vector2& mouseMove) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct ViewRect {

			Vector2 dstPos;   // ウィンドウ内の貼り付け左上
			Vector2 dstSize;  // ウィンドウ内の貼り付けサイズ
			Vector2 srcSize;  // 元サイズ
			InputViewCoordinateSpace coordinateSpace = InputViewCoordinateSpace::Client;
		};

		//--------- variables ----------------------------------------------------

		std::unordered_map<InputViewArea, ViewRect> viewRects_;
	};
}
