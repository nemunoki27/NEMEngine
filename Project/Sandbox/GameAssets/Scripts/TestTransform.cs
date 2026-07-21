using NEMEngine;

namespace TD4_2Scripts;

//============================================================================
//	IrisTransitionTest
//============================================================================
public sealed class IrisTransitionTest : ScriptBehaviour {

	[SeparatorText("テスト設定")]
	[Label("中間進捗")]
	[Range(0.0f, 1.0f)]
	[SerializeField]
	private float testProgress = 0.5f;

	[Label("覆った後の待機時間")]
	[Min(0.0f)]
	[SerializeField]
	private float coveredWaitDuration = 2.0f;

	[Label("遷移先シーン")]
	[SerializeField]
	private SceneAsset? nextScene;

	[Label("単独操作中の入力停止を無効")]
	[SerializeField]
	private bool disableInputBlockForManualTest = true;

	private IrisTransition? irisTransition;
	private IrisTransitionState previousState;
	private TestSequence sequence;
	private float coveredElapsed;
	private bool configuredBlockInput;
	private bool restoreManualInput;

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {

		irisTransition = GetComponent<IrisTransition>();
		if (irisTransition == null) {
			Debug.LogError("[IrisTransitionTest] Iris Transitionと同じEntityに追加してください");
			return;
		}

		previousState = irisTransition.State;
		configuredBlockInput = irisTransition.BlockInput;
		ApplyManualInputSetting(irisTransition);
		Debug.Log("[IrisTransitionTest] F1=IrisOut、F2=IrisIn、F3=中間進捗、F4=停止");
		Debug.Log("[IrisTransitionTest] F5=Play停止、F6=シーン遷移、F7=リセット、Space=往復");
		if (configuredBlockInput && disableInputBlockForManualTest) {
			Debug.Log("[IrisTransitionTest] 単独操作中のみ入力停止を無効化します");
		}
		OutputState(irisTransition);
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		IrisTransition? transition = irisTransition;
		if (transition == null) {
			return;
		}

		transition.SetProgress(1.0f);
		transition.IrisIn();

		UpdateInput(transition);
		UpdateSequence(transition);
		RestoreManualInputSetting(transition);
		if (previousState != transition.State) {
			previousState = transition.State;
			OutputState(transition);
		}
	}

	// 入力によるテスト操作を更新する
	private void UpdateInput(IrisTransition transition) {

		if (Input.GetKeyDown(KeyCode.F1)) {
			PrepareManualTest(transition);
			transition.IrisOut();
			Debug.Log("[IrisTransitionTest] IrisOut");
		}
		if (Input.GetKeyDown(KeyCode.F2)) {
			PrepareManualTest(transition);
			transition.IrisIn();
			Debug.Log("[IrisTransitionTest] IrisIn");
		}
		if (Input.GetKeyDown(KeyCode.F3)) {
			PrepareManualTest(transition);
			transition.SetProgress(testProgress);
			Debug.Log($"[IrisTransitionTest] 進捗={testProgress}");
		}
		if (Input.GetKeyDown(KeyCode.F4)) {
			PrepareManualTest(transition);
			transition.Cancel();
			Debug.Log("[IrisTransitionTest] 停止");
		}
		if (Input.GetKeyDown(KeyCode.F7)) {
			PrepareManualTest(transition);
			transition.Reset();
			Debug.Log("[IrisTransitionTest] リセット");
		}
		if (Input.GetKeyDown(KeyCode.Space)) {
			BeginSequence(transition, TestSequence.RoundTrip);
			Debug.Log("[IrisTransitionTest] IrisOutからIrisInの往復を開始");
		}
		if (Input.GetKeyDown(KeyCode.F6)) {
			BeginSceneTransition(transition);
		}
	}

	// 自動テストを更新する
	private void UpdateSequence(IrisTransition transition) {

		if (sequence == TestSequence.None || !transition.IsCovered) {
			return;
		}

		coveredElapsed += Time.UnscaledDeltaTime;
		if (coveredElapsed < coveredWaitDuration) {
			return;
		}

		TestSequence current = sequence;
		StopSequence();
		if (current == TestSequence.RoundTrip) {
			transition.IrisIn();
			restoreManualInput = true;
			Debug.Log("[IrisTransitionTest] 待機終了、IrisIn");
			return;
		}

		Debug.Log($"[IrisTransitionTest] 待機終了、シーン遷移、自動IrisIn={transition.AutoIrisInAfterSceneTransition}");
		SceneManager.LoadScene(nextScene);
	}

	// 自動テストを開始する
	private void BeginSequence(IrisTransition transition, TestSequence nextSequence) {

		sequence = nextSequence;
		coveredElapsed = 0.0f;
		restoreManualInput = false;
		transition.BlockInput = configuredBlockInput;
		transition.IrisOut();
	}

	// シーン遷移テストを開始する
	private void BeginSceneTransition(IrisTransition transition) {

		if (nextScene == null) {
			Debug.LogError("[IrisTransitionTest] 遷移先シーンを設定してください");
			return;
		}

		BeginSequence(transition, TestSequence.SceneTransition);
		Debug.Log("[IrisTransitionTest] IrisOutからシーン遷移を開始");
	}

	// 単独操作の入力設定を適用する
	private void PrepareManualTest(IrisTransition transition) {

		StopSequence();
		restoreManualInput = false;
		ApplyManualInputSetting(transition);
	}

	// 単独操作用の入力設定を適用する
	private void ApplyManualInputSetting(IrisTransition transition) {

		if (disableInputBlockForManualTest) {
			transition.BlockInput = false;
		}
	}

	// 自動往復後に単独操作用の入力設定へ戻す
	private void RestoreManualInputSetting(IrisTransition transition) {

		if (!restoreManualInput || transition.State != IrisTransitionState.Open) {
			return;
		}

		restoreManualInput = false;
		ApplyManualInputSetting(transition);
	}

	// 自動テストを停止する
	private void StopSequence() {

		sequence = TestSequence.None;
		coveredElapsed = 0.0f;
	}

	// 現在状態を出力する
	private static void OutputState(IrisTransition transition) {

		Debug.Log($"[IrisTransitionTest] 状態={transition.State}、進捗={transition.Progress}、" +
			$"再生中={transition.IsPlaying}、画面を覆った={transition.IsCovered}");
	}

	//--------- structure ----------------------------------------------------

	private enum TestSequence {

		None,
		RoundTrip,
		SceneTransition,
	}
}
