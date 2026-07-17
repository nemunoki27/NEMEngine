using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	UITransitionTableTest
//============================================================================
public sealed class UITransitionTableTest : ScriptBehaviour {

	[SeparatorText("参照")]
	[Label("Canvas")]
	[SerializeField]
	private Entity? canvasEntity;

	[Label("Image Button")]
	[SerializeField]
	private Entity? imageButtonEntity;

	[Label("Text Button")]
	[SerializeField]
	private Entity? textButtonEntity;

	private Canvas? canvas;
	private UIImageButton? imageButton;
	private UITextButton? textButton;

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {

		canvas = canvasEntity?.GetComponent<Canvas>();
		imageButton = imageButtonEntity?.GetComponent<UIImageButton>();
		textButton = textButtonEntity?.GetComponent<UITextButton>();

		if (canvas == null || imageButton == null || textButton == null) {
			Debug.LogError("[UITransitionTableTest] UI参照が不足しています");
			return;
		}

		imageButton.ActionName = "Image";
		textButton.ActionName = "Text";
		Debug.Log("[UITransitionTableTest] 矢印/WASD/D-Pad/左Stick長押しで連続移動、F1で方式切り替え");
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		if (imageButton?.WasClicked == true) {
			Debug.Log($"[UITransitionTableTest] Click={imageButton.ActionName}");
		}
		if (textButton?.WasClicked == true) {
			Debug.Log($"[UITransitionTableTest] Click={textButton.ActionName}");
		}
		if (Input.GetKeyDown(KeyCode.F1) && canvas != null) {
			canvas.NavigationMode = canvas.NavigationMode == CanvasNavigationMode.Automatic ?
				CanvasNavigationMode.TransitionTable : CanvasNavigationMode.Automatic;
			Debug.Log($"[UITransitionTableTest] Navigation={canvas.NavigationMode}");
		}
	}
}
