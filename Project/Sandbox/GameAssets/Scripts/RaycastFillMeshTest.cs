using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	RaycastFillMeshTest
//============================================================================
public sealed class RaycastFillMeshTest : ScriptBehaviour {

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {
	}

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		// レイ設定
		Ray ray = new Ray();
		ray.origin = transform.localPosition + Vector3.up * 4.0f;
		ray.direction = Vector3.down;

		if (Physics.Raycast(ray, out RaycastHit hit, float.PositiveInfinity,
			uint.MaxValue, RaycastTargets.FillMeshes)) {

			Debug.Log("FillMeshと衝突した");
		} else {
			Debug.Log("FillMeshと衝突していない");
		}
	}
}
