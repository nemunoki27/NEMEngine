namespace NEMEngine;

public sealed unsafe partial class UIImageButton : IUIButton {

	// このフレームに決定入力で押されたか
	public bool ClickedThisFrame =>
		NativeAPI.GetUIButtonClicked != null &&
		NativeAPI.GetUIButtonClicked(native, 0) != 0;

	// このフレームに決定入力で押されたか
	public bool WasClicked => ClickedThisFrame;
}
