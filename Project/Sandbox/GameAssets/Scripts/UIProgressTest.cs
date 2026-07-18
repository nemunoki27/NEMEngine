using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	UIProgressTest
//============================================================================
[FormerlyKnownScriptType("SandboxScripts.UIProgress")]
public sealed class UIProgressTest : ScriptBehaviour {

	[SeparatorText("テスト設定")]
	[Label("キー変化量")]
	[Min(0.0f)]
	[SerializeField]
	private float keyChangeAmount = 10.0f;

	[Label("自動往復")]
	[SerializeField]
	private bool automatic;

	[Label("片道時間")]
	[Min(0.01f)]
	[SerializeField]
	private float automaticDuration = 2.0f;

	private NEMEngine.UIProgress? progress;
	private float automaticDirection = 1.0f;

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {

		if (GetComponent<Canvas>() == null) {
			Debug.LogError("[UIProgressTest] Canvasに追加してください");
			return;
		}
		progress = GetComponentInChildren<NEMEngine.UIProgress>();
		if (progress == null) {
			Debug.LogError("[UIProgressTest] Canvas配下にUI Progressがありません");
			return;
		}

		Debug.Log($"[UIProgressTest] 対象={progress.entity.name}");
		Debug.Log("[UIProgressTest] -=減少、=で増加、Homeで最小、Endで最大、F3で自動往復、F4で値を表示");
		OutputValue();
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		if (progress == null) {
			return;
		}

		if (Input.GetKeyDown(KeyCode.Minus)) {
			automatic = false;
			SetValue(progress.Value - keyChangeAmount);
		}
		if (Input.GetKeyDown(KeyCode.Equals)) {
			automatic = false;
			SetValue(progress.Value + keyChangeAmount);
		}
		if (Input.GetKeyDown(KeyCode.Home)) {
			automatic = false;
			SetValue(progress.MinValue);
		}
		if (Input.GetKeyDown(KeyCode.End)) {
			automatic = false;
			SetValue(progress.MaxValue);
		}
		if (Input.GetKeyDown(KeyCode.F3)) {
			automatic = !automatic;
			Debug.Log($"[UIProgressTest] 自動往復={automatic}");
		}
		if (Input.GetKeyDown(KeyCode.F4)) {
			OutputValue();
		}

		if (automatic) {
			UpdateAutomatic();
		}
	}

	// 自動往復を更新する
	private void UpdateAutomatic() {

		if (progress == null) {
			return;
		}

		const float minimumDuration = 0.01f;
		const float threshold = 0.00001f;
		const float minimum = 0.0f;

		const float maximum = 1.0f;
		float range = progress.MaxValue - progress.MinValue;
		if (NEMEngine.Math.Abs(range) <= threshold) {
			progress.Value = progress.MinValue;
			return;
		}

		float duration = NEMEngine.Math.Max(automaticDuration, minimumDuration);
		float normalized = (progress.Value - progress.MinValue) / range;
		normalized += automaticDirection * Time.UnscaledDeltaTime / duration;
		if (normalized >= maximum) {
			normalized = maximum;
			automaticDirection = -1.0f;
		} else if (normalized <= minimum) {
			normalized = minimum;
			automaticDirection = 1.0f;
		}

		progress.Value = progress.MinValue + range * normalized;
	}

	// 値を設定する
	private void SetValue(float value) {

		if (progress == null) {
			return;
		}

		float minimum = NEMEngine.Math.Min(progress.MinValue, progress.MaxValue);
		float maximum = NEMEngine.Math.Max(progress.MinValue, progress.MaxValue);
		progress.Value = NEMEngine.Math.Clamp(value, minimum, maximum);
		OutputValue();
	}

	// 現在値を出力する
	private void OutputValue() {

		if (progress == null) {
			return;
		}

		Debug.Log($"[UIProgressTest] 値={progress.Value}, 表示正規化={progress.DisplayedNormalizedValue}, " +
			$"遅延正規化={progress.DelayedNormalizedValue}");
	}
}
