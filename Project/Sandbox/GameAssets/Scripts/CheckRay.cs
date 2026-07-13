using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	CheckRay
//============================================================================
public sealed class CheckRay : ScriptBehaviour {

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {

	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {
		// レイチェック
		Ray ray = new Ray();
		ray.origin = transform.position;
		ray.direction = Vector3.down;

		if (Physics.Raycast(ray, out RaycastHit hit)) {
			Debug.Log("当たっています");
		} else {
			Debug.Log("当たっていないです");
		}
	}
}
