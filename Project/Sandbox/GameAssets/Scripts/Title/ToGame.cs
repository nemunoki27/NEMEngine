using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	ToGame
//============================================================================
public sealed class ToGame : ScriptBehaviour {

	[Label("決定入力後の遷移遅延")]
	[SerializeField]
	private float toGameDelay = 0.0f;
	// 経過時間
	private float toGameElapsed = 0.0f;
	// 決定入力されたか
	private bool submitted = false;
	// ゲーム終了が選択されたか
	private bool quitSelected = false;

	[Label("開始文字")]
	[SerializeField]
	private UISelectable? startText;
	[Label("終了文字")]
	[SerializeField]
	private UISelectable? endText;

	[SerializeField]
	private SceneAsset gameScene = null!;

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {

		submitted = false;
		quitSelected = false;
		toGameElapsed = 0.0f;
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		// 入力チェック
		CheckSubmit();
		// 入力後の更新
		UpdateSubmitted();
	}

	//========================================================================
	//	決定入力検知
	//========================================================================
	private void CheckSubmit() {

		// 入力があったらなにも受け付けない
		if (submitted) {
			return;
		}

		// 決定入力でゲームシーンに進む
		if (startText?.SubmittedThisFrame == true) {

			submitted = true;
		}
		if (endText?.SubmittedThisFrame == true) {

			submitted = true;
			quitSelected = true;
			Application.Quit();
		}
	}

	//========================================================================
	//	入力後の更新
	//========================================================================
	private void UpdateSubmitted() {

		// 入力された後の処理
		if (!submitted || quitSelected) {
			return;
		}

		// 時間経過を進める
		toGameElapsed += Time.DeltaTime;
		// 時間経過後シーン遷移
		if (toGameDelay < toGameElapsed) {

			SceneManager.LoadScene(gameScene);
		}
	}
}
