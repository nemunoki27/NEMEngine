namespace NEMEngine;

public sealed unsafe partial class UITextButton : IUIButton {

	// このフレームに決定入力で押されたか
	public bool ClickedThisFrame =>
		NativeAPI.GetUIButtonClicked != null &&
		NativeAPI.GetUIButtonClicked(entity.native, 1) != 0;

	// このフレームに決定入力で押されたか
	public bool WasClicked => ClickedThisFrame;
}
