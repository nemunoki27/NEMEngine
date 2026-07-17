namespace NEMEngine;

public interface IUIButton {

	bool Enabled { get; set; }
	string ActionName { get; set; }
	bool ClickedThisFrame { get; }
	bool WasClicked { get; }
}
