#pragma once

//============================================================================
//	include
//============================================================================
#include "InputDeviceState.h"
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

namespace Engine { class WinApp; }

namespace Engine {

	//============================================================================
	//	InputHardware class
	//	入力機器と取得済みframe状態を所有する
	//============================================================================
	class InputHardware {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 入力機器を初期化
		void Init(WinApp& winApp);
		// 前frameのマウス状態を保存
		void BeginFrame();
		// キーボードとゲームパッドを取得
		void PollKeyboardAndGamepads(float deadZone);
		// マウスを取得
		void PollMouse(WinApp& winApp);

		//--------- accessor -----------------------------------------------------

		const InputDeviceState& GetState() const { return state_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<IDirectInput8> dInput_;
		ComPtr<IDirectInputDevice8> keyboard_;
		ComPtr<IDirectInputDevice8> mouse_; // マウスデバイス
		InputDeviceState state_;
	};
}
