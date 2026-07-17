namespace NEMEngine;

public sealed unsafe partial class UIButton {

	// このフレームにポインターまたは決定入力で押されたか
	public bool WasClicked => ClickedThisFrame;
}
