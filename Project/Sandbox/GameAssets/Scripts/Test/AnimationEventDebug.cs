using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	AnimationEventDebug
//	EventA/EventB/EventC発火時にパラメータとエンティティ名をログ出力する
//============================================================================
public sealed class AnimationEventDebug : ScriptBehaviour {

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		// アニメーション入力
		if (Input.GetKeyDown(KeyCode.Alpha0)) {
			if (entity.Name == "TestEventAnim") {

				AnimationPlayer player = entity.GetComponent<AnimationPlayer>();
				player.Play("AnimGroup");
			}
		}
	}
	//========================================================================
	//	アニメーションイベント受信
	//========================================================================
	public override void OnAnimationEvent(AnimationEvent evt) {

		switch (evt.Name) {
			case "EventA":
			case "EventB":
			case "EventC":
				Debug.Log($"[AnimEvent] {evt.Name}  entity={entity.Name}  float={evt.FloatParam}  int={evt.IntParam}  string=\"{evt.StringParam}\"");
				break;
			default:
				Debug.Log($"[AnimEvent] (未登録) {evt.Name}  entity={entity.Name}");
				break;
		}
	}
}
