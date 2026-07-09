using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	RotateAnim
//============================================================================
public sealed class RotateAnim : ScriptBehaviour {
	private AnimationPlayer? animPlayer;

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {
		animPlayer = GetComponent<AnimationPlayer>();
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {
		if (animPlayer == null) {
			return;
		}
		if (Input.GetKeyDown(KeyCode.Alpha1)) {

			animPlayer.Play("Rotate");
		}
	}
}