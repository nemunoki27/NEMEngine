namespace NEMEngine;

public sealed unsafe partial class UIImageButton : IUIButton {

	// このフレームに決定入力で押されたか
	public bool ClickedThisFrame =>
		NativeApi.GetUIButtonClicked != null &&
		NativeApi.GetUIButtonClicked(entity.native, 0) != 0;

	// このフレームに決定入力で押されたか
	public bool WasClicked => ClickedThisFrame;
}
