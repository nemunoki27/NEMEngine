namespace NEMEngine;

public sealed unsafe partial class UITextButton : IUIButton {

	// このフレームに決定入力で押されたか
	public bool WasClicked => ClickedThisFrame;
}
