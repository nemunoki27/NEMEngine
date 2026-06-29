using NEMEngine;
using System.Collections.Generic;

namespace SandboxScripts;

//============================================================================
//	FillMeshCircleTest
//============================================================================
public sealed class FillMeshCircleTest : ScriptBehaviour {

	// 円の分割数
	[SerializeField]
	private int divisions = 32;
	// 円の半径
	[SerializeField]
	private float radius = 2.0f;
	// メッシュ構築フラグ
	[SerializeField]
	private bool build = false;
	// 面の色
	[SerializeField]
	private Color4 fillColor = Color4.white;

	// 円状の点
	[SerializeField]
	private List<Vector3> circlePoints = new List<Vector3>();

	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		if (!build) {
			return;
		}
		build = false;
		if (!entity.TryGet<FillMeshRenderer>(out FillMeshRenderer fill)) {
			return;
		}

		// 円上に点を配置
	}
}