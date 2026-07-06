using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	PlayerAnimChange
//============================================================================
public sealed class PlayerAnimChange : ScriptBehaviour {
	private SkinnedAnimation? skinAnim;

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {
		skinAnim = GetComponent<SkinnedAnimation>();
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		if (Input.GetKeyDown(KeyCode.Alpha0)) {

			skinAnim.Play("walk");
			skinAnim.TransitionDuration = 1.0f;
		}
		if (Input.GetKeyDown(KeyCode.Alpha1)) {

			skinAnim.Play("idle");
			skinAnim.TransitionDuration = 2.0f;
		}

		if (skinAnim.Finished) {

			Debug.Log("アニメーションが終了しました:" + skinAnim.Clip);
		}
	}
}