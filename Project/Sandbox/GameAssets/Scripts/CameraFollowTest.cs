using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	CameraFollowTest
//============================================================================
public sealed class CameraFollowTest : ScriptBehaviour {

	private CameraController? cameraController;

	// 追従対象
	[SerializeField]
	private List<Entity> followTargets;

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {
		cameraController = GetComponent<CameraController>();
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {
		if (cameraController != null) {

			// 追従ターゲットの切り替え
			if (Input.GetKeyDown(KeyCode.Alpha0)) {

				cameraController.Follow.Target = followTargets[0];
			}
			if (Input.GetKeyDown(KeyCode.Alpha1)) {

				cameraController.Follow.Target = followTargets[1];
			}
			if (Input.GetKeyDown(KeyCode.Alpha2)) {

				cameraController.Follow.Target = followTargets[2];
			}
			if (Input.GetKeyDown(KeyCode.Alpha3)) {

				cameraController.Follow.Target = followTargets[3];
			}
			// 追従のオフ/オン
			if (Input.GetKeyDown(KeyCode.Alpha5)) {

				cameraController.Follow.Enabled = false;
			}
			if (Input.GetKeyDown(KeyCode.Alpha6)) {

				cameraController.Follow.Enabled = true;
			}
		}
	}
}
