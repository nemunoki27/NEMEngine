using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	UIEventTest
//============================================================================
[FormerlyKnownScriptType("SandboxScripts.UIEvent")]
public sealed class UIEventTest : ScriptBehaviour {

	[SerializeField]
	private List<UISelectable> selectables = new();

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {

		if (GetComponent<Canvas>() == null) {
			Debug.LogError("[UIEventTest] Canvasに追加してください");
			return;
		}
		selectables = GetComponentsInChildren<UISelectable>();
		if (selectables.Count == 0) {
			Debug.LogError("[UIEventTest] Canvas配下にUI Selectableがありません");
			return;
		}
		Debug.Log($"[UIEventTest] UI Selectable数={selectables.Count}");
		Debug.Log("[UIEventTest] F2で先頭UIの有効と無効を切り替えます");
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		if (selectables.Count == 0) {
			return;
		}

		if (Input.GetKeyDown(KeyCode.F2)) {
			UISelectable target = selectables[0];
			target.Interactable = !target.Interactable;
			Debug.Log($"[UIEventTest] {target.entity.name} 操作可能={target.Interactable}");
		}

		foreach (UISelectable selectable in selectables) {
			if (selectable.NormalThisFrame) {
				OutputState(selectable, UISelectableState.Normal);
			}
			if (selectable.SelectedThisFrame) {
				OutputState(selectable, UISelectableState.Selected);
			}
			if (selectable.SubmittedThisFrame) {
				OutputState(selectable, UISelectableState.Submitted);
			}
			if (selectable.DisabledThisFrame) {
				OutputState(selectable, UISelectableState.Disabled);
			}
		}
	}

	// 状態を出力する
	private static void OutputState(UISelectable selectable, UISelectableState state) {

		Debug.Log($"[UIEventTest] {selectable.entity.name} 状態={GetStateName(state)}");
	}

	// 状態名を取得する
	private static string GetStateName(UISelectableState state) {

		return state switch {
			UISelectableState.Normal => "通常",
			UISelectableState.Selected => "選択",
			UISelectableState.Submitted => "決定",
			UISelectableState.Disabled => "無効",
			_ => "不明",
		};
	}
}
