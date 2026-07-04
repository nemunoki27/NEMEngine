using NEMEngine;
using System.Security.Cryptography;

namespace SandboxScripts;

//============================================================================
//	PlayerMove
//============================================================================
public sealed class PlayerMove : ScriptBehaviour {

	[Label("移動速度")]
	[SerializeField]
	private float moveSpeed = 4.0f;

	// アニメーションクリップ再生
	private AnimationPlayer? animClipPlayer;

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {

		animClipPlayer = GetComponent<AnimationPlayer>();
	}
	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		// アニメーション入力
		if (Input.GetKeyDown(KeyCode.Space) && !animClipPlayer.IsPlaying) {

			animClipPlayer.Play("Test");
		}

		// イージング動作確認、EasingTypeとtからイージング済みの値を返す
		float eased = Easing.Evaluate(EasingType.EaseOutQuad, 0.5f);

		// 前方移動
		transform.localPosition += transform.forward * moveSpeed * eased * Time.deltaTime;
	}
}