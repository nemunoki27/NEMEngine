namespace NEMEngine;

public sealed unsafe partial class UIImageButton : IUIButton {

	// このフレームに決定入力で押されたか
	public bool WasClicked => ClickedThisFrame;
}
